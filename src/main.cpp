#include <iostream>
#include <stdexcept>
#include "config.hpp"
#include "backend_pool.hpp"
#include "event_loop.hpp"

int main(int argc, char* argv[]) {
    const std::string config_path =
        (argc >= 2) ? argv[1] : "config/helios.conf";

    std::cout
        << "  _   _      _ _           \n"
        << " | | | | ___| (_) ___  ___ \n"
        << " | |_| |/ _ \\ | |/ _ \\/ __|\n"
        << " |  _  |  __/ | | (_) \\__ \\\n"
        << " |_| |_|\\___|_|_|\\___/|___/\n"
        << "  TCP Load Balancer  v0.4.0 \n\n";

    // --- Load + validate config ---
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

    // --- Build backend pool ---
    BackendPool pool(cfg);

    for (const auto& b : pool.all())
        std::cout << "[pool ] backend  " << b->host << ":" << b->port << "\n";
    std::cout << "\n";

    // --- Start epoll event loop (blocks until Ctrl-C) ---
    try {
        EventLoop loop(cfg, pool);
        loop.run();
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }

    std::cout << "[helios] shutdown complete.\n";
    return 0;
}
