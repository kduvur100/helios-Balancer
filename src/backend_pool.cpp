#include "backend_pool.hpp"

#include <limits>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

BackendPool::BackendPool(const Config& cfg)
    : algorithm_(cfg.algorithm)
{
    if (cfg.backends.empty())
        throw std::runtime_error("BackendPool: no backends in config");

    backends_.reserve(cfg.backends.size());
    for (const auto& addr : cfg.backends) {
        auto b   = std::make_shared<Backend>();
        b->host  = addr.host;
        b->port  = addr.port;
        backends_.push_back(std::move(b));
    }
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

std::shared_ptr<Backend> BackendPool::pick() {
    switch (algorithm_) {
        case Algorithm::RoundRobin:       return pick_round_robin();
        case Algorithm::LeastConnections: return pick_least_connections();
    }
    return nullptr; // unreachable
}

// ---------------------------------------------------------------------------
// Round-robin
//
// Uses an atomic counter so concurrent picks never block each other.
// We scan up to N backends starting from the cursor to skip unhealthy ones.
// ---------------------------------------------------------------------------

std::shared_ptr<Backend> BackendPool::pick_round_robin() {
    const size_t n = backends_.size();

    for (size_t attempt = 0; attempt < n; ++attempt) {
        // Atomically advance the cursor and wrap it with modulo.
        size_t idx = rr_index_.fetch_add(1, std::memory_order_relaxed) % n;
        auto&  b   = backends_[idx];

        if (b->healthy.load(std::memory_order_acquire))
            return b;
    }
    return nullptr; // all backends down
}

// ---------------------------------------------------------------------------
// Least-connections
//
// Scans all healthy backends and returns the one with the fewest active
// connections at this instant.  No mutex needed: each field is atomic.
// ---------------------------------------------------------------------------

std::shared_ptr<Backend> BackendPool::pick_least_connections() {
    std::shared_ptr<Backend> best;
    int                      best_count = std::numeric_limits<int>::max();

    for (auto& b : backends_) {
        if (!b->healthy.load(std::memory_order_acquire))
            continue;

        int count = b->active_connections.load(std::memory_order_relaxed);
        if (count < best_count) {
            best_count = count;
            best       = b;
        }
    }
    return best; // nullptr if all down
}
