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

## Design Decisions

Real architectural trade-offs made during implementation, including alternatives considered and reasons for each choice.

### Decision 1 — epoll Edge-Triggered Mode (`EPOLLET`) vs. Level-Triggered (`EPOLLLT`)
**Chose:** `EPOLLET` (edge-triggered) in `platform/epoll.c`  
**Alternatives considered:** Level-triggered epoll (the default); `poll()`/`select()` for simplicity  
**Why:** Level-triggered fires on every `epoll_wait` call while data remains available — under high concurrency with hundreds of active fds this generates redundant wakeups that waste CPU cycles. Edge-triggered fires exactly once per state change, forcing the worker to drain each fd completely to `EAGAIN`. This eliminates spurious wakeups at the cost of stricter application logic.  
**Trade-off:** The worker loop *must* read in a loop until `EAGAIN` on every event; a single missed `EAGAIN` causes the fd to silently stop receiving events until the next state change. This makes the code more fragile — a partial-read bug becomes a hung connection, not a slow one.

### Decision 2 — Lock-Free Round-Robin with `_Atomic int` vs. Mutex
**Chose:** `atomic_fetch_add_explicit(&route->rr_index, 1, memory_order_relaxed)` in `balancer.c`  
**Alternatives considered:** `pthread_mutex_t` guard around index increment; consistent hashing for sticky sessions  
**Why:** A mutex would serialize all 32 worker threads on every backend selection — even a fast mutex adds ~50–100 ns of contention overhead per connection. `atomic_fetch_add` compiles to a single `LOCK XADD` CPU instruction with no thread blocking. Consistent hashing would have required re-hashing all active sessions on every backend change, which breaks the requirement that config reload must not interrupt active connections.  
**Trade-off:** The counter wraps on overflow (guarded with `abs()`), so under an intense burst a single backend may receive one extra request before the cycle resets. Not strictly fair in microsecond windows, but statistically correct over any meaningful time period.

### Decision 3 — RCU-Style Atomic Config Swap vs. Reader-Writer Lock
**Chose:** `_Atomic(SharedState*)` pointer with `atomic_exchange` swap in `main.c`  
**Alternatives considered:** `pthread_rwlock_t` (readers share, writer exclusive); stop-the-world reload (restart the process)  
**Why:** A reader-writer lock adds a lock/unlock pair to every connection accept — on the hot path with thousands of connections per second, even a read lock has measurable overhead. The `_Atomic` pointer load is a single `MOV` with no synchronization overhead for readers. Restarting the process would drop all active connections, violating the requirement that reload must be transparent.  
**Trade-off:** Old `SharedState` memory cannot be freed immediately — it must remain valid until all workers that loaded it before the swap have finished using it. This requires a grace-period or reference-count mechanism, adding lifecycle complexity that is absent with a mutex approach.

### Decision 4 — Thread-Per-Core Worker Pool vs. Single-Threaded Event Loop
**Chose:** N worker threads each owning their own `epoll` fd + `SO_REUSEPORT` on listener sockets  
**Alternatives considered:** Single-threaded event loop (Node.js model); work-stealing thread pool with a shared `epoll` fd  
**Why:** A single-threaded event loop saturates at one CPU core regardless of hardware. A shared `epoll` fd requires cross-thread fd migration when handing off accepted connections to workers — this adds latency and requires locking on the shared epoll instance. Thread-per-core eliminates both bottlenecks: `SO_REUSEPORT` lets the kernel distribute incoming connections across worker sockets at the NIC level without any userspace coordination, and each worker's private `epoll` fd has zero contention.  
**Trade-off:** Higher baseline memory (each worker needs its own fd table, stack, and event buffer). The thundering-herd problem on `accept()` is real on older kernels, but `SO_REUSEPORT` with `SOCK_NONBLOCK` on Linux ≥ 3.9 routes new connections to exactly one socket, eliminating it.

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

### Configure

```bash
cp proxy.toml.example proxy.toml
# Edit proxy.toml to add your backends
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

### Code Style

```bash
# Format all source files in-place
meson compile -C builddir format

# Check formatting (CI-safe, exits non-zero if reformatting needed)
meson compile -C builddir format-check
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

### Docker Deploy

```bash
# Build the image
docker build -t proxy-epoll-iocp:latest .

# Run with your config
docker run -d \
  --name revproxy \
  -p 8080:8080 \
  -v $(pwd)/proxy.toml:/app/proxy.toml:ro \
  proxy-epoll-iocp:latest

# Deploy to GCI VM (requires SSH access)
docker save proxy-epoll-iocp:latest | \
  ssh -i ~/.ssh/vboxuser gcvmuser@34.174.56.186 "docker load"

ssh -i ~/.ssh/vboxuser gcvmuser@34.174.56.186 \
  "docker run -d --name revproxy --restart=unless-stopped \
   --network miseia-net \
   -v ~/proxy.toml:/app/proxy.toml:ro \
   proxy-epoll-iocp:latest"
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

---

## Updates — 2026-06-29

- **`index.html`** — new self-contained HTML landing page describing the project: architecture diagram, key features, tech stack, quick-start guide, and buttons linking to both the GitHub (`https://github.com/Jorgeaapaz/MISEIA_1-6-10-revproxy-c`) and GitLab (`https://gitlab.codecrypto.academy/jorgeaapaz/MISEIA_1-6-10-revproxy-c`) repositories. Dark terminal aesthetic, no external CDN dependencies.
- **`docs/prompts/feature_001_html_project_page_prompt.md`** — disciplined implementation prompt (feature 001) that drove the `index.html` creation, following the project's standard prompt template.

---

## AI Collaboration

This project was built with [Claude Code](https://claude.ai/code) (Sonnet 4.6) as an AI assistant. See [`docs/AI_COLLABORATION.md`](docs/AI_COLLABORATION.md) for a module-by-module log of what the AI generated, what the developer changed, and the patterns of errors caught during review.

---

## Updates — 2026-06-08

- **`RUN_PROJECT_AND_TEST.md`** — new file: complete step-by-step guide for building, running, and testing the proxy on Windows (IOCP backend). Covers prerequisites, PATH setup, MSYS2 bash compile workflow, unit tests, manual proxy run, dynamic config reload, integration tests, load test, troubleshooting table, and known limitations.
- **`RETROSPECTIVA-2026-05-02.md`** — added Sesión 3 section documenting: what `mock_server` does (single-threaded HTTP echo server, `Connection: close`, name-in-body assertion pattern), the mock servers' role as the actual routing targets, the full `Start-Job → HttpWebRequest → proxy.exe → mock_server.exe` call chain, the `ServicePointManager.DefaultConnectionLimit` problem and why `Start-Job` (separate processes) was chosen over `RunspacePool`, and the p99 latency measurement limitation with `HttpWebRequest`.
