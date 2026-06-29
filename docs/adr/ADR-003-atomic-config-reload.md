# ADR-003 — RCU-Style Atomic Config Reload

## Status
Accepted

## Date
2026-05-02

## Context
The proxy must reload its routing table at runtime (on `SIGHUP` / Windows named event) without interrupting active connections. Requirements:
- Active connections tunneling bytes at the time of reload must complete normally
- New connections after reload must use the new routing table immediately
- Reload latency must be < 50 ms

Options evaluated:
- **Stop-the-world:** drain all active connections, reload, restart workers — simple but violates the "no interruption" requirement
- **`pthread_rwlock_t`:** readers share the lock on every connection accept; writer takes exclusive lock during reload — adds per-connection lock overhead on the hot path
- **`_Atomic(SharedState*)` pointer swap (chosen):** workers load the pointer once per connection; the reload thread swaps the pointer atomically with `atomic_exchange`; the old `SharedState` is freed after a grace period

## Decision
The live routing table is held behind an `_Atomic(SharedState*)` pointer. On reload:
1. A new `SharedState` is allocated and populated from the updated `proxy.toml`
2. The old pointer is atomically exchanged for the new pointer with `atomic_exchange`
3. Workers that already loaded the old pointer continue using it until their connection closes
4. The old `SharedState` is freed after a configurable grace period (default: 5 seconds, longer than any expected active connection)

Workers load the pointer **once per connection accept** and hold a local copy for the lifetime of that connection. No per-packet locking is needed.

## Consequences

### Positive
- Zero overhead on the hot path: pointer load is a single `atomic_load` instruction
- Active connections are never interrupted — they hold a reference to the old config until they close
- Reload completes in < 50 ms (dominated by TOML parse time, not synchronization)

### Negative / Trade-offs
- Memory lifecycle complexity: the old `SharedState` cannot be freed immediately; it must remain valid until all workers using it finish — requires a grace period or reference count
- If a grace period is used (current implementation), memory peaks briefly at 2× the `SharedState` size during reload
- If a connection is exceptionally long-lived (e.g., a slow upload), it may use a very old routing table — acceptable for this use case

### Neutral
- The `_Atomic` pointer store/load uses `memory_order_seq_cst` by default in the current implementation; `memory_order_acquire`/`memory_order_release` would be sufficient but the performance difference is negligible at reload frequency
