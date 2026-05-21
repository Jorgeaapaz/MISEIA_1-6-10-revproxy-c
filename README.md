# proxy-epoll-iocp

A high-performance **Layer 7 reverse proxy** written in C11 that routes HTTP traffic by domain name using native async I/O — `epoll` on Linux, IOCP on Windows. Supports multiple listening ports, per-domain round-robin load balancing, and zero-downtime configuration reload.

---

## Features Implemented

### Multi-Port Listening
Accept connections on up to 16 independent TCP ports simultaneously. Each listener is registered with the platform event loop and serviced by a configurable pool of worker threads (defaults to CPU count).

### Domain-Based Routing (Layer 7)
Inspect the HTTP `Host:` header and match it against configured routes using a three-tier precedence:
1. **Exact match** — `api.example.com`
2. **Wildcard suffix** — `*.example.com` (matches any subdomain)
3. **Global fallback** — `*` catch-all

### Atomic Round-Robin Load Balancing
Each route can have multiple backend servers. A per-route `_Atomic int` counter selects the next backend with a lock-free fetch-and-add, ensuring fair distribution even under high concurrency.

### Dynamic Configuration Reload
Send `SIGHUP` (Linux) or trigger a Windows Event to reload `proxy.toml` at runtime. The shared state is swapped atomically — active connections are never interrupted, and new connections immediately use the updated routing table.

### Platform-Agnostic Event Loop
A thin abstraction (`event_loop.h`) hides the platform differences. `platform/epoll.c` uses edge-triggered `epoll`, and `platform/iocp.c` uses Windows I/O Completion Ports. The core proxy code is identical on both platforms.

---

## Project Structure

```
proxy-epoll-iocp/
├── src/
│   ├── main.c          — Entry point; worker threads, accept loop, config reload
│   ├── config.c/h      — Hand-written TOML parser; Config struct loading & validation
│   ├── router.c/h      — Domain route lookup (exact / wildcard / global)
│   ├── balancer.c/h    — Atomic round-robin backend selection
│   ├── tunnel.c/h      — Connection state machine & bidirectional byte forwarding
│   ├── dispatcher.c/h  — HTTP header parser; extracts Host: header value
│   ├── listener.c/h    — TCP socket creation with SO_REUSEADDR / SO_REUSEPORT
│   ├── log.c/h         — Structured logging (trace/debug/info/warn/error)
│   ├── utils.c/h       — Portable time, string, and socket helpers
│   ├── event_loop.h    — Platform-agnostic event loop interface
│   └── meson.build     — Builds proxy_core static lib + proxy executable
├── platform/
│   ├── epoll.c         — Linux epoll backend (edge-triggered, per-worker fd)
│   └── iocp.c          — Windows IOCP backend (completion port based)
├── tests/
│   ├── test_router.c       — Router unit tests (exact, wildcard, precedence)
│   ├── test_balancer.c     — Balancer unit tests (round-robin, thread safety)
│   ├── test_config.c       — Config unit tests (TOML parsing, validation)
│   ├── mock_server.c       — Minimal HTTP server for integration testing
│   ├── run_integration.sh  — Integration test script (Linux)
│   ├── run_integration.ps1 — Integration test script (Windows)
│   ├── run_loadtest.ps1    — Load testing script
│   └── meson.build         — Test configuration
├── docs/
│   └── requirements.md     — Formal functional and non-functional requirements
├── proxy.toml          — Example proxy configuration
├── proxy_test.toml     — Test configuration
├── SPEC.md             — Detailed architecture and module specification
├── meson.build         — Root build; platform detection, subdir inclusion
└── meson.options       — User-configurable build options
```

---

## Design Patterns / Architecture

**State Machine (tunnel.c)**
Each connection progresses through four states: `READING_HEADER → CONNECTING_BACKEND → TUNNELING → CLOSING`. Every call to `tunnel_pump()` advances the machine based on the current state and which fd fired an event. This keeps all per-connection logic in one place with no nested callbacks.

**Atomic RCU-style Config Swap (main.c)**
The live routing table is held behind an `_Atomic(SharedState*)` pointer. On reload, a new `SharedState` is built from the updated file and swapped in with `atomic_exchange`. Workers load the pointer once per connection accept; no locks, no blocking.

**Thread-Per-Core Worker Pool (main.c)**
The main thread handles signals and reload. N worker threads each own their event loop instance (an `epoll` fd on Linux, an IOCP handle on Windows). All workers accept from the shared listener sockets independently, eliminating a single-threaded accept bottleneck.

**Opaque Platform Abstraction (event_loop.h)**
`EventLoop` is a forward-declared opaque struct. Platform implementations (`epoll.c`, `iocp.c`) define the concrete type. Core code only calls `evloop_create / add / mod / del / wait / destroy`, making a third backend (e.g., `kqueue`) a drop-in addition.

**Lock-Free Load Balancing (balancer.c)**
Backend selection uses `atomic_fetch_add_explicit(..., memory_order_relaxed)` on a per-route counter. No mutex is needed; the fetch-and-add is atomic, and modulo gives the next server index. Negative-wrap overflow is handled with an absolute-value guard.

**Ring Buffer Forwarding (tunnel.c)**
Two fixed-size buffers (`c2b_buf`, `b2c_buf`) with length and offset fields enable partial writes. When a `send()` returns a short count, the offset advances and the remaining bytes are flushed on the next writable event — no heap allocation per I/O operation.

---

## How It Works

On startup the proxy loads `proxy.toml`, builds a route lookup table, and creates listening sockets. Each worker thread enters an event loop; when a client connects, the HTTP `Host:` header is read, matched against the route table, and a backend is selected atomically. From that point the worker ferries bytes in both directions until one side closes, then tears down the connection.

```c
// Core per-event dispatch in main.c worker loop
evloop_wait(el, events, MAX_EVENTS, 200);   // block up to 200 ms
for (int i = 0; i < nev; i++) {
    FdCtx *ctx = events[i].ctx;
    if (ctx->kind == FD_LISTENER)
        accept_client(ctx, el, state);      // new connection
    else
        tunnel_pump(ctx->conn, events[i].events, el, state); // drive state machine
}
```

**Configuration snippet (`proxy.toml`):**
```toml
[global]
workers            = 4
connect_timeout_ms = 5000
read_timeout_ms    = 30000
log_level          = "info"
forwarded_for      = true

[[listener]]
port = 80

[[route]]
domain   = "api.test"
backends = ["127.0.0.1:9001", "127.0.0.1:9002"]

[[route]]
domain   = "web.test"
backends = ["127.0.0.1:9003"]

[[route]]
domain   = "*"
backends = ["127.0.0.1:9000"]
```

---

## Getting Started

### Prerequisites

| Tool | Version |
|------|---------|
| C compiler | GCC ≥ 10 / Clang ≥ 12 / MSVC 2022 |
| Meson | ≥ 1.0 |
| Ninja | ≥ 1.10 |
| Python | ≥ 3.8 (required by Meson) |

On Windows, install [MinGW-w64](https://www.mingw-w64.org/) or use MSVC. A `mingw64.ini` cross-file is provided in the repo root.

### Clone

```bash
git clone https://github.com/Jorgeaapaz/MISEIA_1-6-10-revproxy-c.git
cd MISEIA_1-6-10-revproxy-c
```

### Build

```bash
# Configure (release mode)
meson setup builddir -Dbuildtype=release

# Compile
meson compile -C builddir

# Run unit tests
meson test -C builddir
```

Cross-compile for Windows from Linux using the provided MinGW cross-file:

```bash
meson setup builddir-win --cross-file mingw64.ini -Dbuildtype=release
meson compile -C builddir-win
```

### Run

```bash
# Start proxy with default config
./builddir/proxy --config proxy.toml

# Override log level and write to file
./builddir/proxy --config proxy.toml --log-level debug --log-file proxy.log

# Reload config at runtime (Linux)
kill -HUP $(pgrep proxy)
```

### Integration Tests

```bash
# Linux
bash tests/run_integration.sh

# Windows (PowerShell, run as Administrator for hosts file access)
.\tests\run_integration.ps1
```

---

## Example Output

**Startup:**
```
2026-05-20T10:00:00.000Z [INFO ] [main] loaded config: 4 workers, 1 listener(s), 3 route(s)
2026-05-20T10:00:00.001Z [INFO ] [main] listening on 0.0.0.0:80
2026-05-20T10:00:00.002Z [INFO ] [main] worker 0 started (epoll)
2026-05-20T10:00:00.002Z [INFO ] [main] worker 1 started (epoll)
```

**Successful request routed to backend:**
```
2026-05-20T10:00:05.123Z [INFO ] [worker-0] 127.0.0.1:54312 → api.test → 127.0.0.1:9001 (connected 2 ms)
2026-05-20T10:00:05.456Z [INFO ] [worker-0] 127.0.0.1:54312 closed (351 ms, 1240 B↑ 4096 B↓)
```

**Round-robin across two backends (10 requests to `api.test`):**
```
→ 127.0.0.1:9001   (request 1)
→ 127.0.0.1:9002   (request 2)
→ 127.0.0.1:9001   (request 3)
→ 127.0.0.1:9002   (request 4)
...
```

**Unknown domain — 502 returned:**
```
2026-05-20T10:00:10.000Z [WARN ] [worker-1] 127.0.0.1:54400 no route for domain "unknown.host" → 502
```

**Header timeout exceeded — 408 returned:**
```
2026-05-20T10:00:40.000Z [WARN ] [worker-2] 127.0.0.1:54500 header read timeout (30000 ms) → 408
```

**Config reload:**
```
2026-05-20T10:01:00.000Z [INFO ] [main] SIGHUP received — reloading config
2026-05-20T10:01:00.003Z [INFO ] [main] config reloaded: 3 route(s) → 4 route(s)
```

---

## Non-Functional Targets

| Metric | Target |
|--------|--------|
| p99 latency (LAN) | < 1 ms |
| Concurrent connections | ≥ 10,000 |
| Platforms | Linux (epoll), Windows (IOCP) |
| C standard | C11 |
| Build warnings | Zero (`-Wall -Wextra -Wpedantic`) |
| External dependencies | None (stdlib + OS threads) |

---

## License

MIT
