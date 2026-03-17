# Helios — TCP Load Balancer

A high-performance TCP load balancer written in modern C++17 using Linux `epoll` for non-blocking I/O.

```
clients
  │
  ▼
┌────────────────────────────────┐
│         Helios (epoll)         │
│   accept → route → forward     │
└──────┬──────────┬──────────────┘
       │          │          │
  backend:3001  backend:3002  backend:3003
```

## Features

- **Non-blocking I/O** via Linux `epoll` edge-triggered mode
- **Round-robin** and **least-connections** algorithms
- **Health checker** background thread
- **Graceful shutdown** on `SIGINT` / `SIGTERM`
- INI-style config file, no external dependencies

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/helios config/helios.conf
```


