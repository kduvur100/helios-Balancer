#pragma once

#include <memory>
#include <atomic>
#include <ctime>

#include "backend.hpp"

// ---------------------------------------------------------------------------
// ConnState — lifecycle of one proxied TCP session
//
//  CONNECTING  backend connect() issued, waiting for EPOLLOUT to confirm
//  ACTIVE      tunnel is live, data flows in both directions
//  CLOSING     one side issued EOF / error, tearing down
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
//   conn_map_ in EventLoop maps fd → shared_ptr<Connection>.
//   Both client_fd and backend_fd point to the same object so an event on
//   either fd can immediately reach the peer fd.
//
// Half-close tracking:
//   When one side sends EOF we shutdown(SHUT_WR) the peer fd and set the
//   corresponding flag.  The session is torn down once both sides are done.
//
// Thread safety:
//   All access is from the single event-loop thread — no locking needed.
// ---------------------------------------------------------------------------
struct Connection {
    int client_fd  = -1;   // accepted client socket
    int backend_fd = -1;   // non-blocking socket to upstream server

    ConnState state = ConnState::CONNECTING;

    // Half-close flags: set when that direction has seen EOF
    bool client_eof  = false;
    bool backend_eof = false;

    // Byte counters for logging / metrics
    uint64_t bytes_client_to_backend = 0;
    uint64_t bytes_backend_to_client = 0;

    // Which backend we chose (decremented on close)
    std::shared_ptr<Backend> backend;

    // Creation timestamp for idle-timeout eviction (Day 6+)
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
