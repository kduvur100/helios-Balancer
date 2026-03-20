#pragma once

#include <atomic>
#include <memory>
#include <unordered_map>

#include "config.hpp"
#include "backend_pool.hpp"
#include "connection.hpp"

// ---------------------------------------------------------------------------
// EventLoop — epoll-driven accept + async-connect engine.
//
// Day 3 scope:
//   - Accept incoming client connections (edge-triggered EPOLLET drain)
//   - Open a non-blocking connect() to the chosen backend
//   - Register both fds in conn_map_ so events on either side resolve to
//     the same Connection object
//   - Detect connect completion via EPOLLOUT on the backend fd
//   - Tear down connections cleanly via close_connection()
//
// Day 4 will extend handle_active() to splice data bidirectionally.
//
// Design notes:
//   - Single-threaded event loop; no locking on conn_map_ needed.
//   - conn_map_ is keyed by fd — O(1) lookup from either end of a session.
//   - close_connection() removes both entries atomically so no dangling fds.
// ---------------------------------------------------------------------------
class EventLoop {
public:
    EventLoop(const Config& cfg, BackendPool& pool);
    ~EventLoop();

    // Blocks until stop() is called or a fatal error occurs.
    void run();

    // Thread-safe: may be called from a signal handler or another thread.
    void stop() { stop_flag_.store(true, std::memory_order_relaxed); }

private:
    // --- Accept stage ---
    void accept_all();
    int  connect_to_backend(const Backend& b);

    // --- Event dispatch ---
    void handle_event(int fd, uint32_t events);
    void handle_connect(Connection& conn);           // CONNECTING → ACTIVE
    void handle_active(Connection& conn, int fd,
                       uint32_t events);             // Day 4: data forwarding

    // --- Cleanup ---
    void close_connection(Connection& conn);

    // --- epoll helpers ---
    void epoll_add(int fd, uint32_t events, bool edge_triggered = true);
    void epoll_mod(int fd, uint32_t events, bool edge_triggered = true);
    void epoll_del(int fd);

    // --- State ---
    int           epoll_fd_  = -1;
    int           listen_fd_ = -1;
    const Config& cfg_;
    BackendPool&  pool_;

    // Both client_fd and backend_fd of a session map to the same Connection.
    std::unordered_map<int, std::shared_ptr<Connection>> conn_map_;

    std::atomic<bool> stop_flag_{false};
};
