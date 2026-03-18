#pragma once

#include <string>
#include <atomic>

// Represents one upstream server.
// All mutable fields are either atomic or protected by BackendPool's mutex —
// this struct is shared across threads (event loop + health checker).
struct Backend {
    std::string host;
    int         port = 0;

    // Incremented when a connection is assigned, decremented on close.
    // Used by the least-connections algorithm.
    std::atomic<int>  active_connections{0};

    // Set false by the health checker when the backend stops responding.
    // The event loop skips unhealthy backends during selection.
    std::atomic<bool> healthy{true};

    Backend() = default;

    // Movable (needed for emplace_back), but not copyable — atomics aren't.
    Backend(Backend&& o) noexcept
        : host(std::move(o.host))
        , port(o.port)
        , active_connections(o.active_connections.load())
        , healthy(o.healthy.load())
    {}

    Backend& operator=(Backend&&) = delete;
    Backend(const Backend&)       = delete;
    Backend& operator=(const Backend&) = delete;
};
