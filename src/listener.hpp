#pragma once

#include <string>
#include "config.hpp"

// Creates a non-blocking TCP listen socket bound to cfg.bind_host:cfg.bind_port.
// Returns the file descriptor on success; throws std::runtime_error on any error.
//
// Socket options set:
//   SO_REUSEADDR  — allows fast restart without waiting for TIME_WAIT to expire
//   SO_REUSEPORT  — lets multiple processes bind the same port (useful later)
//   O_NONBLOCK    — required for edge-triggered epoll; accept() returns EAGAIN
//                   instead of blocking when no more connections are pending
int create_listen_socket(const Config& cfg);
