#pragma once

#include <atomic>
#include "config.hpp"
#include "backend_pool.hpp"

// Owns the epoll file descriptor and drives the entire accept/dispatch loop.
//
// Design notes:
//  - Edge-triggered (EPOLLET): we must drain accept() until EAGAIN on every
//    EPOLLIN event, otherwise we miss connections.
//  - stop_flag_ is checked after each epoll_wait timeout (1 s) so the loop
//    exits cleanly when the process receives a signal (Day 6).
//  - Day 3 scope: accept connections and log them. Actual forwarding to
//    backends is wired up in Day 4.
class EventLoop {
public:
    EventLoop(const Config& cfg, BackendPool& pool);
    ~EventLoop();

    // Blocks until stop() is called or a fatal error occurs.
    void run();

    // Thread-safe: may be called from a signal handler or another thread.
    void stop() { stop_flag_.store(true, std::memory_order_relaxed); }

private:
    // Drain all pending connections on the listen socket (edge-triggered).
    void accept_all();

    int           epoll_fd_  = -1;
    int           listen_fd_ = -1;
    const Config& cfg_;
    BackendPool&  pool_;

    std::atomic<bool> stop_flag_{false};
};
