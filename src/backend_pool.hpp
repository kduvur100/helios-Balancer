#pragma once

#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <stdexcept>

#include "backend.hpp"
#include "config.hpp"

// BackendPool owns all Backend instances and implements the load-balancing
// selection algorithms.  Thread-safe: pick() may be called concurrently from
// multiple event-loop threads.
class BackendPool {
public:
    explicit BackendPool(const Config& cfg);

    // Select the next backend according to the configured algorithm.
    // Returns nullptr if no healthy backend exists.
    std::shared_ptr<Backend> pick();

    // Convenience: return a reference to every backend (e.g. for health checks).
    const std::vector<std::shared_ptr<Backend>>& all() const { return backends_; }

    size_t size() const { return backends_.size(); }

private:
    std::shared_ptr<Backend> pick_round_robin();
    std::shared_ptr<Backend> pick_least_connections();

    std::vector<std::shared_ptr<Backend>> backends_;
    Algorithm                             algorithm_;

    // Round-robin cursor — fetch_add wraps naturally via modulo.
    std::atomic<size_t> rr_index_{0};
};
