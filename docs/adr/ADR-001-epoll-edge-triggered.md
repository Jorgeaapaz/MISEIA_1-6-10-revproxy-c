# ADR-001 — epoll Edge-Triggered Mode

## Status
Accepted

## Date
2026-05-02

## Context
The proxy must handle thousands of concurrent client connections per worker thread on Linux. `epoll` supports two notification modes:
- **Level-triggered (LT):** fires on every `epoll_wait` call while data remains available in the fd buffer
- **Edge-triggered (ET):** fires exactly once per state change (new data arriving, write space becoming available)

Under high concurrency with hundreds of active fds, level-triggered mode generates wakeups even when the application cannot make forward progress — for example, when a connection is waiting for a backend and holds data in the receive buffer. These spurious wakeups waste CPU cycles and reduce the effective throughput per worker thread.

The proxy uses a non-blocking socket model where the worker loop processes all ready events in a batch and must then block efficiently in `epoll_wait`.

## Decision
Use `EPOLLET` (edge-triggered) mode exclusively in `platform/epoll.c`. Each fd is registered with `EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP`. The worker loop drains each fd in a loop until `read()`/`recv()` returns `-1` with `errno == EAGAIN` before moving to the next event.

## Consequences

### Positive
- Eliminates spurious wakeups: each event corresponds to a genuine state change
- Enables higher throughput per worker thread under sustained load
- Consistent with the thread-per-core model (each worker processes its fd set without interference)

### Negative / Trade-offs
- Mandatory drain-to-`EAGAIN`: every read/write handler must loop until `EAGAIN`; a partial drain silently stops the fd from firing until the next state change — bugs manifest as hung connections, not slow ones
- Higher code complexity: the state machine in `tunnel.c` must carefully track partial reads and buffer ownership across multiple `EAGAIN` cycles
- Debugging difficulty: edge-triggered bugs do not produce error messages; the connection simply stops responding

### Neutral
- `EPOLLRDHUP` is added to detect peer half-close without waiting for the next read to return 0
- `EPOLLONESHOT` was considered but rejected: it requires re-arming after every event, adding complexity without benefit given the per-worker fd ownership model
