#include "config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace {

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string strip_comment(const std::string& s) {
    auto pos = s.find('#');
    return (pos == std::string::npos) ? s : s.substr(0, pos);
}

bool split_kv(const std::string& line, std::string& key, std::string& value) {
    auto pos = line.find('=');
    if (pos == std::string::npos) return false;
    key   = trim(line.substr(0, pos));
    value = trim(line.substr(pos + 1));
    return !key.empty();
}

bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(),
        [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) ==
                   std::tolower(static_cast<unsigned char>(y));
        });
}

} // namespace

Config Config::from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open config file: " + path);

    Config cfg;
    BackendAddr* current_backend = nullptr;
    std::string line;
    int line_no = 0;

    while (std::getline(file, line)) {
        ++line_no;
        line = trim(strip_comment(line));
        if (line.empty()) continue;

        if (line == "[backend]") {
            cfg.backends.emplace_back();
            current_backend = &cfg.backends.back();
            continue;
        }
        if (line.front() == '[') {
            current_backend = nullptr;
            continue;
        }

        std::string key, value;
        if (!split_kv(line, key, value))
            throw std::runtime_error("Malformed line " +
                std::to_string(line_no) + ": \"" + line + "\"");

        if (current_backend) {
            if (key == "host") { current_backend->host = value; continue; }
            if (key == "port") { current_backend->port = std::stoi(value); continue; }
            // Non-backend key resets section
            current_backend = nullptr;
        }

        if      (key == "bind_host")          cfg.bind_host          = value;
        else if (key == "bind_port")          cfg.bind_port          = std::stoi(value);
        else if (key == "backlog")            cfg.backlog            = std::stoi(value);
        else if (key == "max_events")         cfg.max_events         = std::stoi(value);
        else if (key == "health_interval_ms") cfg.health_interval_ms = std::stoi(value);
        else if (key == "health_timeout_ms")  cfg.health_timeout_ms  = std::stoi(value);
        else if (key == "algorithm") {
            if      (iequal(value, "round_robin"))       cfg.algorithm = Algorithm::RoundRobin;
            else if (iequal(value, "least_connections")) cfg.algorithm = Algorithm::LeastConnections;
            else throw std::runtime_error("Unknown algorithm: " + value);
        }
    }
    return cfg;
}

void Config::validate() const {
    if (backends.empty())
        throw std::runtime_error("Config must define at least one [backend]");
    if (bind_port <= 0 || bind_port > 65535)
        throw std::runtime_error("Invalid bind_port: " + std::to_string(bind_port));
    for (const auto& b : backends) {
        if (b.host.empty())
            throw std::runtime_error("A [backend] is missing a host");
        if (b.port <= 0 || b.port > 65535)
            throw std::runtime_error("Invalid backend port: " + std::to_string(b.port));
    }
    if (health_interval_ms <= 0)
        throw std::runtime_error("health_interval_ms must be positive");
}
