# AI Collaboration Log — proxy-epoll-iocp

**AI assistant used:** Claude Code (Sonnet 4.6)  
**Development period:** 2026-05-02 → 2026-06-29  
**Reference retrospectives:** `RETROSPECTIVA-2026-05-02.md`

This document tracks which parts of the project were AI-generated, what the human developer changed from the AI draft, and why.

---

## Development Model

The project was built with Claude Code as an AI assistant. The workflow was:
1. AI generated initial implementations based on `SPEC.md` and `docs/requirements.md`
2. Developer reviewed, compiled, and tested each module
3. Bugs found in testing were diagnosed by the developer and corrected
4. Some corrections were made directly by the developer; others were prompted back to the AI with explicit bug reports

The AI was strong at generating structurally correct C11 code following the spec. The developer was the authority on runtime behavior — bugs only surfaced under actual execution.

---

## Module-by-Module Review

### `src/config.c` — TOML Parser

**AI draft:** Correct parser logic, struct layout, and validation rules. Generated all 6 test cases in `test_config.c`.

**Human change — Stack overflow on `Config` struct:**  
The AI allocated `Config cfg` on the stack in test functions. `Config` holds 1024 routes × 64 backends × 256 bytes = **~16 MB** stack frame. This caused `STATUS_STACK_OVERFLOW (0xC00000FD)` on Windows.  
**Fix:** Developer changed all stack-allocated `Config cfg` to `Config *cfg = calloc(1, sizeof(Config))` with matching `free()`. The AI missed this because it didn't model Windows's default 1 MB thread stack limit.

**Accepted as-is:** Parser logic, validation for missing listener/route, port range checking, backend format validation, default `[global]` values.

---

### `platform/iocp.c` — Windows IOCP Event Loop

**AI draft:** Structurally correct use of `WSAEventSelect` + `WSAWaitForMultipleEvents`. The architecture (one completion port per worker) was correctly derived from the spec.

**Human change — WSAWaitForMultipleEvents auto-reset bug (most critical bug in project):**  
The proxy blocked indefinitely after processing the first connection. Root cause: the AI's `evloop_wait` called `WSAWaitForMultipleEvents` twice — once to detect which event fired, then again per-fd to enumerate network events. The internal re-call consumed the wakeup event (auto-reset on second wait), causing the loop to block permanently.  
**Fix:** Developer diagnosed via `OutputDebugString` tracing and rewrote `evloop_wait` to call `WSAWaitForMultipleEvents` exactly once, manually reset the wakeup event if index 0 fired, then enumerate all fds with `WSAEnumNetworkEvents` in a single pass.

**Accepted as-is:** Overall IOCP architecture, fd context management, event registration/deregistration API.

---

### `src/tunnel.c` — Bidirectional Byte Forwarding

**AI draft:** State machine (`READING_HEADER → CONNECTING_BACKEND → TUNNELING → CLOSING`) with buffer management. Core logic was correct.

**Human change — CONN_TUNNELING flush gated on EV_WRITE (second critical bug):**  
On Windows, `FD_WRITE` only fires *after* `WSAEWOULDBLOCK` is received. If the send socket had space from the start, `FD_WRITE` never fired — data sat in `c2b_buf`/`b2c_buf` until a new event arrived. Connections hung after the first request.  
**Fix:** Developer changed `CONN_TUNNELING` handler to flush both buffers unconditionally on every event, not gated on `EV_WRITE`. This is the correct behavior for edge-triggered I/O: always drain when you have data, regardless of which event triggered the pump.

**Accepted as-is:** Ring buffer logic (offset + length for partial writes), state machine transitions, EOF handling.

---

### `tests/mock_server.c` — Mini HTTP Backend

**AI draft:** Correct single-threaded accept loop, HTTP response format, name-in-body pattern.

**Human change — Missing `#include <stdint.h>`:**  
`uint16_t` was used but `<stdint.h>` was not included. Compilation failed on MinGW with "undeclared identifier". Simple one-line fix but the AI missed it because it doesn't always track which headers are implicit on each platform.

**Human change — Backlog limit discovery:**  
The AI set `listen(sock, 64)`. Developer discovered via load testing that above ~30 concurrent clients the backlog saturated, causing connection failures. This was left at 64 (appropriate for test use); the retrospective documents why production backends would be different.

**Accepted as-is:** HTTP response format, `Connection: close` behavior, name assertion pattern.

---

### `src/utils.h` / `src/main.c` — Platform Utilities

**Human change — `sock_errno()` missing:**  
`main.c` called `sock_errno()` for portable errno handling (`errno` on Linux, `WSAGetLastError()` on Windows). The AI had used it throughout but forgotten to define it in `utils.h`. Added the `#ifdef _WIN32` macro guard.

**Accepted as-is:** `time_ms()`, `parse_hostport()`, socket type abstraction (`socket_t`).

---

### `src/log.c` — Structured Logger

**Human change — Redundant `(void)file; (void)line;` casts:**  
The AI generated `(void)file; (void)line;` to suppress unused-variable warnings, then also used `file` and `line` in the `fprintf` call below. The suppression casts were incorrect and removed.

**Accepted as-is:** ISO8601 timestamp format, 5-level filtering, thread-safe `flockfile`/`funlockfile`.

---

### `tests/run_loadtest.ps1` — Load Test Orchestration

**AI draft:** Initial `RunspacePool`-based concurrent load test.

**Human diagnosis — `ServicePointManager.DefaultConnectionLimit = 2` bottleneck:**  
364 of 500 requests failed with the RunspacePool approach. Developer diagnosed via systematic elimination that `[System.Net.ServicePointManager]::DefaultConnectionLimit` defaults to 2 connections per host *per process*, and all RunspacePool threads share one process. Setting the limit higher helped partially but inconsistently.  
**Fix:** Developer decided to replace `RunspacePool` with `Start-Job` (separate child processes). Each child process has its own `DefaultConnectionLimit`, giving 2 connections × 20 processes = 40 simultaneous connections. 500/500 success.

**Human discovery — Windows Defender false positive:**  
Defender flagged the load test script twice (deleted it mid-execution). Developer identified the triggers: `Substring()` in `catch` blocks + `RunspacePool` + rapid HTTP loops. Fixed by eliminating `Substring()` in catch blocks and switching to `Start-Job` (different heuristic profile per process).

---

## Patterns of AI Errors Caught

1. **Platform stack size blind spot:** AI generated large stack-allocated structs without modeling OS-specific stack limits (Windows: 1 MB default vs. Linux: 8 MB default).

2. **Windows event semantics:** AI's IOCP implementation was architecturally correct but wrong on a subtle `WSAWaitForMultipleEvents` auto-reset behavior. AI understood the API but not the interaction between multiple calls on the same event handle.

3. **Edge-triggered I/O discipline:** AI gated buffer flushes on `EV_WRITE`, which is correct for level-triggered but wrong for edge-triggered (Windows IOCP variant). The correct rule — always flush when you have data — was applied by the developer.

4. **Missing headers:** AI reliably generated correct logic but occasionally omitted platform-specific includes (`<stdint.h>`) that are implicitly available on some compilers.

5. **Dead code generation:** `(void)var;` suppression casts generated even when the variable was subsequently used.

---

## What AI Did Well

- **Architecture from spec:** Given `SPEC.md` and `docs/requirements.md`, the AI produced a correct module decomposition (config/router/balancer/dispatcher/tunnel/event_loop) on the first attempt.
- **Test case generation:** All 72 unit test cases (router × 8, balancer × 24, config × 40) were AI-generated and are correct.
- **Cross-platform abstractions:** The `event_loop.h` opaque interface and `platform/` split were AI-designed and correctly separate platform concerns from core logic.
- **TOML parser:** Hand-written TOML parser with validation — complex to write correctly, accepted with no changes to logic.
- **Documentation:** `SPEC.md`, `docs/requirements.md`, `README.md` drafts produced by AI and refined by developer.
- **Integration test scripts:** `run_integration.ps1` and `run_integration.sh` structure and test case coverage were AI-generated; the developer fixed only a port conflict and the `sock_errno` reference.

---

## Critical Review Checklist Applied by Developer

- [x] Memory management (malloc/free balance) — found `Config` stack overflow
- [x] Error path completeness (all fds closed on error) — verified in tunnel.c
- [x] Atomic operation memory ordering — `memory_order_relaxed` reviewed; correct for counter use
- [x] Thread safety of shared state — atomic pointer swap verified correct
- [x] Windows/Linux platform compatibility — IOCP bug found via actual execution on Windows
- [x] Edge-triggered I/O drain discipline — flush-gating bug found and fixed
- [x] Load test reliability — `ServicePointManager` limit identified and worked around
