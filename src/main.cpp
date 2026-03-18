#include <iostream>
#include <stdexcept>
#include "config.hpp"
#include "backend_pool.hpp"

int main(int argc, char* argv[]) {
    const std::string config_path =
        (argc >= 2) ? argv[1] : "config/helios.conf";

    std::cout
        << "  _   _      _ _           \n"
        << " | | | | ___| (_) ___  ___ \n"
        << " | |_| |/ _ \\ | |/ _ \\/ __|\n"
        << " |  _  |  __/ | | (_) \\__ \\\n"
        << " |_| |_|\\___|_|_|\\___/|___/\n"
        << "  TCP Load Balancer  v0.2.0 \n\n";

    Config cfg;
    try {
        cfg = Config::from_file(config_path);
        cfg.validate();
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }

    const char* algo_name =
        (cfg.algorithm == Algorithm::RoundRobin) ? "round_robin" : "least_connections";

    std::cout
        << "[config] bind       : " << cfg.bind_host << ":" << cfg.bind_port << "\n"
        << "[config] algorithm  : " << algo_name << "\n"
        << "[config] backends   : " << cfg.backends.size() << "\n"
        << "[config] health     : every " << cfg.health_interval_ms << " ms\n\n";

    // -----------------------------------------------------------------------
    // Day 2: Build the backend pool and exercise the selection algorithms
    // -----------------------------------------------------------------------
    BackendPool pool(cfg);

    std::cout << "[pool] Registered " << pool.size() << " backend(s):\n";
    for (const auto& b : pool.all())
        std::cout << "       " << b->host << ":" << b->port << "\n";

    // Simulate 9 picks so we can see round-robin cycling across 3 backends
    std::cout << "\n[pool] Simulating 9 picks (" << algo_name << "):\n";
    for (int i = 0; i < 9; ++i) {
        auto b = pool.pick();
        if (b)
            std::cout << "  pick[" << i << "] -> "
                      << b->host << ":" << b->port
                      << "  (active=" << b->active_connections.load() << ")\n";
        else
            std::cout << "  pick[" << i << "] -> NO HEALTHY BACKEND\n";
    }

    // Simulate marking backend[1] unhealthy and verify it is skipped
    std::cout << "\n[pool] Marking backend[1] unhealthy...\n";
    pool.all()[1]->healthy.store(false);

    std::cout << "[pool] Simulating 6 picks with backend[1] down:\n";
    for (int i = 0; i < 6; ++i) {
        auto b = pool.pick();
        if (b)
            std::cout << "  pick[" << i << "] -> "
                      << b->host << ":" << b->port << "\n";
        else
            std::cout << "  pick[" << i << "] -> NO HEALTHY BACKEND\n";
    }

    std::cout << "\n[helios] BackendPool ready — epoll event loop coming in Day 3.\n";
    return 0;
}
