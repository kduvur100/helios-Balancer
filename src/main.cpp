#include <iostream>
#include <stdexcept>
#include "config.hpp"

int main(int argc, char* argv[]) {
    const std::string config_path =
        (argc >= 2) ? argv[1] : "config/helios.conf";

    std::cout
        << "  _   _      _ _           \n"
        << " | | | | ___| (_) ___  ___ \n"
        << " | |_| |/ _ \\ | |/ _ \\/ __|\n"
        << " |  _  |  __/ | | (_) \\__ \\\n"
        << " |_| |_|\\___|_|_|\\___/|___/\n"
        << "  TCP Load Balancer  v0.1.0 \n\n";

    Config cfg;
    try {
        cfg = Config::from_file(config_path);
        cfg.validate();
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }

    std::cout
        << "[config] bind       : " << cfg.bind_host << ":" << cfg.bind_port << "\n"
        << "[config] algorithm  : "
        << (cfg.algorithm == Algorithm::RoundRobin ? "round_robin" : "least_connections") << "\n"
        << "[config] backends   : " << cfg.backends.size() << "\n"
        << "[config] health     : every " << cfg.health_interval_ms << " ms\n\n";

    for (size_t i = 0; i < cfg.backends.size(); ++i)
        std::cout << "  backend[" << i << "]  "
                  << cfg.backends[i].host << ":" << cfg.backends[i].port << "\n";

    std::cout << "\n[helios] Framework ready — event loop coming in Day 2.\n";
    return 0;
}
