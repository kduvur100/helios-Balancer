#include "listener.hpp"

#include <stdexcept>
#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Helper: set a file descriptor to non-blocking mode
// ---------------------------------------------------------------------------
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        throw std::runtime_error(std::string("fcntl F_GETFL: ") + strerror(errno));
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        throw std::runtime_error(std::string("fcntl F_SETFL O_NONBLOCK: ") + strerror(errno));
}

// ---------------------------------------------------------------------------
// Helper: setsockopt wrapper that throws on failure
// ---------------------------------------------------------------------------
static void setsock(int fd, int level, int opt, int val, const char* name) {
    if (setsockopt(fd, level, opt, &val, sizeof(val)) == -1)
        throw std::runtime_error(std::string("setsockopt ") + name + ": " + strerror(errno));
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
int create_listen_socket(const Config& cfg) {
    // --- 1. Create socket ---
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
        throw std::runtime_error(std::string("socket: ") + strerror(errno));

    // --- 2. Socket options ---
    setsock(fd, SOL_SOCKET,  SO_REUSEADDR, 1, "SO_REUSEADDR");
    setsock(fd, SOL_SOCKET,  SO_REUSEPORT, 1, "SO_REUSEPORT");
    setsock(fd, IPPROTO_TCP, TCP_NODELAY,  1, "TCP_NODELAY");

    // --- 3. Non-blocking ---
    set_nonblocking(fd);

    // --- 4. Bind ---
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(cfg.bind_port));

    if (inet_pton(AF_INET, cfg.bind_host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        throw std::runtime_error("Invalid bind_host: " + cfg.bind_host);
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        close(fd);
        throw std::runtime_error(std::string("bind: ") + strerror(errno));
    }

    // --- 5. Listen ---
    if (listen(fd, cfg.backlog) == -1) {
        close(fd);
        throw std::runtime_error(std::string("listen: ") + strerror(errno));
    }

    return fd;
}
