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
static constexpr int MAX_EVENTS = 64;

// epoll_wait timeout in ms — short enough that stop() is noticed promptly
static constexpr int EPOLL_TIMEOUT_MS = 1000;

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

    // 2. Bind + listen
    try {
        listen_fd_ = create_listen_socket(cfg_);
    } catch (...) {
        close(epoll_fd_);
        throw;
    }

    // 3. Register the listen socket with epoll
    //    EPOLLIN  — readable (new connection pending)
    //    EPOLLET  — edge-triggered: only fires when state *changes*, so we
    //               must call accept() in a loop until EAGAIN
    epoll_event ev{};
    ev.events   = EPOLLIN | EPOLLET;
    ev.data.fd  = listen_fd_;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) == -1) {
        close(listen_fd_);
        close(epoll_fd_);
        throw std::runtime_error(std::string("epoll_ctl ADD listen_fd: ") + strerror(errno));
    }

    std::cout << "[epoll] listening on "
              << cfg_.bind_host << ":" << cfg_.bind_port
              << "  (fd=" << listen_fd_ << ")\n";
}

EventLoop::~EventLoop() {
    if (listen_fd_ != -1) close(listen_fd_);
    if (epoll_fd_  != -1) close(epoll_fd_);
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
            if (errno == EINTR) continue; // interrupted by signal, check stop flag
            throw std::runtime_error(std::string("epoll_wait: ") + strerror(errno));
        }

        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == listen_fd_) {
                // New connection(s) arrived on the listen socket
                accept_all();
            }
            // Day 4: handle client/backend I/O events here
        }
    }

    std::cout << "[epoll] event loop stopped.\n";
}

// ---------------------------------------------------------------------------
// Accept all pending connections (edge-triggered drain loop)
// ---------------------------------------------------------------------------

void EventLoop::accept_all() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);

        int client_fd = accept4(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len,
            SOCK_NONBLOCK | SOCK_CLOEXEC  // set non-blocking atomically
        );

        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more pending connections — done draining
                break;
            }
            std::cerr << "[epoll] accept4 error: " << strerror(errno) << "\n";
            break;
        }

        // Format client address for logging
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int  client_port = ntohs(client_addr.sin_port);

        // Pick a backend using the configured algorithm
        auto backend = pool_.pick();
        if (!backend) {
            std::cerr << "[epoll] no healthy backend — dropping connection from "
                      << client_ip << ":" << client_port << "\n";
            close(client_fd);
            continue;
        }

        std::cout << "[conn ] accepted " << client_ip << ":" << client_port
                  << "  -> " << backend->host << ":" << backend->port
                  << "  (fd=" << client_fd << ")\n";

        // Day 4: open connection to backend, register both fds with epoll,
        //        and splice data bidirectionally.
        // For now, close immediately to keep the demo clean.
        close(client_fd);
    }
}
