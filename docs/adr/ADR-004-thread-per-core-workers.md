# ADR-004 — Thread-Per-Core Worker Model

## Status
Accepted

## Date
2026-05-02

## Context
The proxy must support ≥ 10,000 simultaneous connections. The event loop model determines how CPU cores are utilized:

- **Single-threaded event loop (Node.js model):** one `epoll` fd, one thread; simple but saturates at one CPU core
- **Shared `epoll` fd with thread pool:** multiple threads call `epoll_wait` on the same fd; the kernel wakes one thread per event (thundering-herd mitigation built in since Linux 4.5), but cross-thread fd migration is needed when load-balancing connections
- **Thread-per-core with `SO_REUSEPORT` (chosen):** each worker thread owns a private `epoll` fd; `SO_REUSEPORT` on listener sockets lets the kernel distribute new connections directly to workers at the socket layer

## Decision
Create N worker threads (default: `nproc`) each with their own `epoll` fd instance. All listener sockets are created with `SO_REUSEPORT`, causing the kernel to load-balance `accept()` calls across worker sockets without userspace involvement. The main thread handles signals, config reload, and worker lifecycle.

On Windows, each worker has its own IOCP completion port (analogous to an `epoll` fd), with `AcceptEx` used to post accept operations to the appropriate port.

## Consequences

### Positive
- Scales linearly with CPU core count — each core runs independently
- No cross-thread fd migration: a connection is owned by one worker from accept to close
- `SO_REUSEPORT` eliminates the thundering-herd problem on `accept()` (kernel distributes connections)
- Private `epoll` fds: no locking on the event loop itself

### Negative / Trade-offs
- Higher baseline memory: each worker maintains its own fd table, stack (8 MB default on Linux), and event buffer array
- Load imbalance possible: the kernel's `SO_REUSEPORT` distribution is based on a hash of source/destination IP+port, which can be uneven for connections from a small set of clients
- More complex worker lifecycle management: crash of one worker's fd context could affect its active connections without impacting other workers (isolation is a feature, but recovery is harder)

### Neutral
- The worker count is runtime-configurable (`workers = N` in `proxy.toml`; 0 = auto-detect)
- A work-stealing model could improve load balance but adds complexity that is not justified for the current scale target (10,000 connections)
