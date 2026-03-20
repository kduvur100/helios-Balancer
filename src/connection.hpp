#pragma once

#include <memory>
#include <atomic>
#include <ctime>

#include "backend.hpp"

// ---------------------------------------------------------------------------
// ConnState — lifecycle of one proxied TCP session
//
//  CONNECTING  backend connect() issued, waiting for EPOLLOUT to confirm
//  ACTIVE      tunnel is live, data flows in both directions (Day 4)
//  CLOSING     one side issued EOF / error, draining then tearing down
// ---------------------------------------------------------------------------
enum class ConnState {
    CONNECTING,
    ACTIVE,
    CLOSING,
};

// ---------------------------------------------------------------------------
// Connection — owns both ends of one client ↔ backend session.
//
// Memory model:
//   conn_map_ in EventLoop maps  fd → shared_ptr<Connection>
//   Both client_fd and backend_fd map to the *same* Connection object so an
//   event on either fd can reach the peer fd immediately.
//
// Thread safety:
//   All access is from the single event-loop thread — no locking needed here.
// ---------------------------------------------------------------------------
struct Connection {
    int client_fd  = -1;   // accepted client socket
    int backend_fd = -1;   // non-blocking socket connecting to upstream

    ConnState state = ConnState::CONNECTING;

    // Which backend we chose (so we can decrement active_connections on close)
    std::shared_ptr<Backend> backend;

    // Monotonic timestamp used for idle-timeout eviction (Day 6+)
    std::time_t created_at = 0;

    Connection(int cfd, int bfd, std::shared_ptr<Backend> b)
        : client_fd(cfd), backend_fd(bfd), backend(std::move(b))
        , created_at(std::time(nullptr))
    {}

    // Non-copyable, non-movable — always accessed via shared_ptr
    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&)                 = delete;
    Connection& operator=(Connection&&)      = delete;
};
