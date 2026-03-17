#pragma once

#include <string>
#include <vector>
#include <stdexcept>

struct BackendAddr {
    std::string host;
    int         port = 0;
};

enum class Algorithm {
    RoundRobin,
    LeastConnections
};

struct Config {
    std::string bind_host          = "0.0.0.0";
    int         bind_port          = 8080;
    int         backlog            = 128;
    int         max_events         = 64;
    Algorithm   algorithm          = Algorithm::RoundRobin;
    int         health_interval_ms = 5000;
    int         health_timeout_ms  = 1000;

    std::vector<BackendAddr> backends;

    static Config from_file(const std::string& path);
    void validate() const;
};
