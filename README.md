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

## Week Roadmap

| Day | Focus |
|-----|-------|
| ✅ 1 | Project scaffold + INI config parser |
| 2 | `BackendPool` — round-robin & least-connections |
| 3 | `epoll` event loop + connection accept |
| 4 | Bidirectional TCP proxy / data forwarding |
| 5 | Health checker background thread |
| 6 | Signal handling + graceful shutdown |
| 7 | Demo script + benchmarks + README polish |
