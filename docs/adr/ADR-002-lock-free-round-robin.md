# ADR-002 — Lock-Free Round-Robin with C11 `_Atomic int`

## Status
Accepted

## Date
2026-05-02

## Context
Each routing rule can have multiple backend servers. Every new client connection must select the next backend. With the thread-per-core model, up to 32 worker threads may select backends for the same route simultaneously.

Options evaluated:
- `pthread_mutex_t` around an integer counter increment
- C11 `_Atomic int` with `atomic_fetch_add`
- Consistent hashing (backend determined by client IP or session cookie)
- Random selection

A mutex serializes all workers on every backend selection. Even a fast uncontended mutex costs ~20–50 ns; under high concurrency with multiple threads selecting the same high-traffic route, lock contention adds measurable latency.

Consistent hashing requires a hash function per connection and re-hashing all active sessions when a backend is added or removed, which conflicts with the requirement that config reload must not interrupt active connections.

## Decision
Use `atomic_fetch_add_explicit(&route->rr_index, 1, memory_order_relaxed)` on a per-route `_Atomic int` counter. Backend index is `abs(counter) % route->nbackends`. The `abs()` guard handles the rare integer overflow case.

`memory_order_relaxed` is correct here: the counter is not protecting any other shared memory — it only needs atomicity on its own value, not ordering with respect to other operations.

## Consequences

### Positive
- Single `LOCK XADD` CPU instruction — zero thread blocking, zero kernel involvement
- Scales linearly: N worker threads selecting backends in parallel have no contention overhead
- Simple to reason about: the only invariant is that each increment is unique

### Negative / Trade-offs
- Not strictly fair in microsecond windows: under a short burst, one backend may receive `N+1` requests before the modulo cycle resets
- Integer overflow: `_Atomic int` will eventually wrap to `INT_MIN`; the `abs()` guard produces a positive index but momentarily "wastes" one increment — negligible in practice
- No session affinity: the same client may be routed to different backends on successive requests; acceptable for the stateless HTTP proxy use case

### Neutral
- Consistent hashing remains a viable extension if sticky sessions are required in a future version; the `balancer.c` interface is narrow enough to swap the implementation
