#include "event_loop.hpp"
#include "listener.hpp"

#include <iostream>
#include <stdexcept>
#include <cstring>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// How many events to harvest per epoll_wait call
static constexpr int  MAX_EVENTS       = 64;
// epoll_wait timeout — short so stop() is noticed promptly
static constexpr int  EPOLL_TIMEOUT_MS = 1000;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

EventLoop::EventLoop(const Config& cfg, BackendPool& pool)
    : cfg_(cfg), pool_(pool)
{
    // 1. Create the epoll instance
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1)
        throw std::runtime_error(std::string("epoll_create1: ") + strerror(errno));

    // 2. Bind + listen (creates a non-blocking socket via listener.cpp)
    try {
        listen_fd_ = create_listen_socket(cfg_);
    } catch (...) {
        close(epoll_fd_);
        throw;
    }

    // 3. Register listen socket: EPOLLIN | EPOLLET
    //    Edge-triggered: fires only on state change, so accept_all() must
    //    drain until EAGAIN or it will miss connections in the same batch.
    epoll_add(listen_fd_, EPOLLIN, /*edge_triggered=*/true);

    std::cout << "[epoll] listening on "
              << cfg_.bind_host << ":" << cfg_.bind_port
              << "  (fd=" << listen_fd_ << ")\n";
}

EventLoop::~EventLoop() {
    // Close all live connections (fds removed from epoll implicitly on close,
    // but we also clear the map so shared_ptrs are released cleanly).
    for (auto& [fd, conn] : conn_map_) {
        if (conn->client_fd  != -1) { close(conn->client_fd);  conn->client_fd  = -1; }
        if (conn->backend_fd != -1) { close(conn->backend_fd); conn->backend_fd = -1; }
    }
    conn_map_.clear();

    if (listen_fd_ != -1) close(listen_fd_);
    if (epoll_fd_  != -1) close(epoll_fd_);
}

// ---------------------------------------------------------------------------
// epoll helpers
// ---------------------------------------------------------------------------

void EventLoop::epoll_add(int fd, uint32_t events, bool edge_triggered) {
    epoll_event ev{};
    ev.events  = events | (edge_triggered ? EPOLLET : 0u);
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1)
        throw std::runtime_error(std::string("epoll_ctl ADD fd=")
                                 + std::to_string(fd) + ": " + strerror(errno));
}

void EventLoop::epoll_mod(int fd, uint32_t events, bool edge_triggered) {
    epoll_event ev{};
    ev.events  = events | (edge_triggered ? EPOLLET : 0u);
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1)
        std::cerr << "[epoll] EPOLL_CTL_MOD fd=" << fd
                  << " error: " << strerror(errno) << "\n";
}

void EventLoop::epoll_del(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void EventLoop::run() {
    std::cout << "[epoll] event loop started — waiting for connections...\n";

    epoll_event events[MAX_EVENTS];

    while (!stop_flag_.load(std::memory_order_relaxed)) {
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, EPOLL_TIMEOUT_MS);

        if (n == -1) {
            if (errno == EINTR) continue; // signal interrupted — re-check stop flag
            throw std::runtime_error(std::string("epoll_wait: ") + strerror(errno));
        }

        for (int i = 0; i < n; ++i) {
            int      fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listen_fd_) {
                accept_all();
            } else {
                handle_event(fd, ev);
            }
        }
    }

    std::cout << "[epoll] event loop stopped — "
              << conn_map_.size() / 2 << " active connections dropped.\n";
}

// ---------------------------------------------------------------------------
// accept_all — EPOLLET drain: call accept4() until EAGAIN
// ---------------------------------------------------------------------------

void EventLoop::accept_all() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);

        // accept4 sets SOCK_NONBLOCK | SOCK_CLOEXEC atomically
        int client_fd = accept4(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len,
            SOCK_NONBLOCK | SOCK_CLOEXEC
        );

        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // fully drained
            std::cerr << "[accept] accept4 error: " << strerror(errno) << "\n";
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int  client_port = ntohs(client_addr.sin_port);

        // Pick a healthy backend (round-robin or least-connections)
        auto backend = pool_.pick();
        if (!backend) {
            std::cerr << "[accept] no healthy backend — rejecting "
                      << client_ip << ":" << client_port << "\n";
            close(client_fd);
            continue;
        }

        // Start a non-blocking connect to the upstream server
        int backend_fd = connect_to_backend(*backend);
        if (backend_fd == -1) {
            std::cerr << "[accept] connect_to_backend failed — dropping "
                      << client_ip << ":" << client_port << "\n";
            close(client_fd);
            continue;
        }

        // Track this backend's load
        backend->active_connections.fetch_add(1, std::memory_order_relaxed);

        // One Connection object — both fds point to it
        auto conn = std::make_shared<Connection>(client_fd, backend_fd, backend);
        conn_map_[client_fd]  = conn;
        conn_map_[backend_fd] = conn;

        // Watch backend_fd for EPOLLOUT: that's how we detect connect() completion.
        // client_fd is registered only after the tunnel goes ACTIVE.
        epoll_add(backend_fd, EPOLLOUT, /*edge_triggered=*/true);

        std::cout << "[conn ] " << client_ip << ":" << client_port
                  << "  connecting →  "
                  << backend->host << ":" << backend->port
                  << "  (client_fd=" << client_fd
                  << "  backend_fd=" << backend_fd << ")\n";
    }
}

// ---------------------------------------------------------------------------
// connect_to_backend — non-blocking TCP connect to an upstream server.
//
// Returns a new fd with connect() in flight, or -1 on immediate failure.
// connect() on a non-blocking socket returns -1/EINPROGRESS; we detect
// completion later via EPOLLOUT (see handle_connect).
// ---------------------------------------------------------------------------

int EventLoop::connect_to_backend(const Backend& b) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        std::cerr << "[backend] socket: " << strerror(errno) << "\n";
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(b.port));

    if (inet_pton(AF_INET, b.host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "[backend] inet_pton failed for host: " << b.host << "\n";
        close(fd);
        return -1;
    }

    int rc = connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (rc == -1 && errno != EINPROGRESS) {
        // Immediate failure (e.g. ECONNREFUSED on loopback — no server running)
        std::cerr << "[backend] connect to " << b.host << ":" << b.port
                  << " failed: " << strerror(errno) << "\n";
        close(fd);
        return -1;
    }

    // rc == 0        → connected instantly (rare on loopback)
    // errno EINPROGRESS → normal async path, EPOLLOUT confirms completion
    return fd;
}

// ---------------------------------------------------------------------------
// handle_event — dispatch epoll events for non-listen fds
// ---------------------------------------------------------------------------

void EventLoop::handle_event(int fd, uint32_t events) {
    auto it = conn_map_.find(fd);
    if (it == conn_map_.end()) {
        // Stale fd (connection already torn down) — remove from epoll
        epoll_del(fd);
        return;
    }

    auto& conn = *it->second;

    // Error or hang-up on any fd → tear down the whole session
    if (events & (EPOLLERR | EPOLLHUP)) {
        if (conn.state == ConnState::CONNECTING) {
            int       err = 0;
            socklen_t len = sizeof(err);
            getsockopt(conn.backend_fd, SOL_SOCKET, SO_ERROR, &err, &len);
            std::cerr << "[conn ] connect error (backend_fd=" << conn.backend_fd
                      << "): " << (err ? strerror(err) : "EPOLLHUP") << "\n";
        }
        close_connection(conn);
        return;
    }

    switch (conn.state) {
        case ConnState::CONNECTING:
            // EPOLLOUT fired on backend_fd — check if connect() succeeded
            handle_connect(conn);
            break;

        case ConnState::ACTIVE:
            handle_active(conn, fd, events);
            break;

        case ConnState::CLOSING:
            close_connection(conn);
            break;
    }
}

// ---------------------------------------------------------------------------
// handle_connect — verify async connect(), transition to ACTIVE.
//
// getsockopt(SO_ERROR) returns the deferred connect error.
// On success:
//   - Swap backend_fd to EPOLLIN (data flowing upstream → client)
//   - Register client_fd for EPOLLIN (data flowing client → upstream)
//   - Mark connection ACTIVE
// ---------------------------------------------------------------------------

void EventLoop::handle_connect(Connection& conn) {
    int       err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(conn.backend_fd, SOL_SOCKET, SO_ERROR, &err, &len) == -1 || err != 0) {
        std::cerr << "[conn ] async connect failed (backend_fd="
                  << conn.backend_fd << "): "
                  << (err ? strerror(err) : strerror(errno)) << "\n";
        close_connection(conn);
        return;
    }

    // Re-arm backend_fd: now we want to read upstream responses
    epoll_mod(conn.backend_fd, EPOLLIN, /*edge_triggered=*/true);

    // Activate client_fd: start receiving data from the client
    epoll_add(conn.client_fd, EPOLLIN, /*edge_triggered=*/true);

    conn.state = ConnState::ACTIVE;

    std::cout << "[conn ] tunnel ACTIVE  "
              << "client_fd=" << conn.client_fd
              << "  backend_fd=" << conn.backend_fd
              << "  backend=" << conn.backend->host << ":" << conn.backend->port
              << "\n";
}

// ---------------------------------------------------------------------------
// handle_active — bidirectional data forwarding (Day 4 placeholder).
//
// Day 4 will implement the splice loop:
//   EPOLLIN on client_fd  → read from client,  write to backend_fd
//   EPOLLIN on backend_fd → read from backend, write to client_fd
//   EOF / error on either → mark CLOSING, flush the other direction
// ---------------------------------------------------------------------------

void EventLoop::handle_active(Connection& conn, int fd, uint32_t events) {
    (void)events;
    std::cout << "[conn ] data ready on fd=" << fd
              << "  (splice forwarding — Day 4)\n";
    // TODO Day 4: read from fd, write to peer fd
    close_connection(conn);
}

// ---------------------------------------------------------------------------
// close_connection — tear down both ends of a proxied session.
//
// Removes both fds from epoll, erases both conn_map_ entries, closes the fds,
// and decrements the backend's active_connections counter.
// Safe to call if either fd is already -1 (idempotent).
// ---------------------------------------------------------------------------

void EventLoop::close_connection(Connection& conn) {
    if (conn.client_fd != -1) {
        epoll_del(conn.client_fd);
        conn_map_.erase(conn.client_fd);
        close(conn.client_fd);
        conn.client_fd = -1;
    }
    if (conn.backend_fd != -1) {
        epoll_del(conn.backend_fd);
        conn_map_.erase(conn.backend_fd);
        close(conn.backend_fd);
        conn.backend_fd = -1;
    }

    // Decrement load counter so the backend isn't unfairly penalised
    if (conn.backend)
        conn.backend->active_connections.fetch_sub(1, std::memory_order_relaxed);

    conn.state = ConnState::CLOSING;
}
