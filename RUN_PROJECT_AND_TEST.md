# Run & Test Guide — Windows (IOCP backend)

This guide covers every step needed to build, run, and test the proxy on Windows using
the MSYS2 / MinGW-w64 toolchain. All commands are grouped by the shell they must be run in.

---

## Prerequisites

| Tool | Version | Installer |
|------|---------|-----------|
| MSYS2 (MinGW-w64 + GCC) | GCC 15.2+ | `winget install --id MSYS2.MSYS2` |
| Meson | ≥ 1.0 | `uv tool install meson` |
| Ninja | ≥ 1.10 | `uv tool install ninja` |
| Python | ≥ 3.8 | required by Meson internally |
| curl | any | included with Windows 10/11 |

### Install MSYS2 and GCC (one-time)

Run in **PowerShell** (Administrator not required for package installs):

```powershell
# 1. Install MSYS2
winget install --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements

# 2. Install MinGW-w64 GCC inside MSYS2
C:\msys64\usr\bin\bash.exe -lc "pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-toolchain"
```

After this you will find `gcc.exe` at `C:\msys64\mingw64\bin\gcc.exe`.

### Install Meson and Ninja (one-time)

```powershell
# uv must already be installed at C:\Users\<you>\.local\bin\uv.exe
uv tool install meson   # -> C:\Users\jorge\.local\bin\meson.exe
uv tool install ninja   # -> C:\Users\jorge\.local\bin\ninja.EXE
```

---

## Critical: which shell to use and why

| Task | Shell | Reason |
|------|-------|--------|
| **Compile / meson setup / meson compile** | **MSYS2 bash** | PowerShell activates MSVC environment variables that crash `cc1.exe` (MinGW's compiler driver), even when `gcc.exe` is on the PATH. Always compile through MSYS2 bash. |
| Run the proxy, mock servers | PowerShell or CMD | Any shell works once the `.exe` is built. |
| Run unit / integration / load tests | PowerShell | Scripts are `.ps1`; use `powershell -ExecutionPolicy Bypass -File ...` |
| Config reload signal | PowerShell | Uses .NET `EventWaitHandle` to set the named Windows Event. |

MSYS2 bash is located at `C:\msys64\usr\bin\bash.exe` (NOT `C:\msys64\mingw64\bin\bash.exe` — that path does not exist).

---

## 1. PATH helper — set once per PowerShell session

Every PowerShell session that touches meson/ninja/gcc must run this first:

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;C:\Users\jorge\.local\bin;' + $env:PATH
$env:CC  = 'C:\msys64\mingw64\bin\gcc.exe'
$env:CXX = 'C:\msys64\mingw64\bin\g++.exe'
cd D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp
```

---

## 2. Build

### 2a. First-time setup — run in MSYS2 bash

```powershell
# Open MSYS2 bash from PowerShell:
$env:PATH = "C:\msys64\usr\bin;" + $env:PATH
bash -c "export PATH=/mingw64/bin:/usr/bin:/c/Users/jorge/.local/bin:`$PATH; cd /d/Master-IA-Dev/06-Bloque6/1-6-10-revproxy-c/proxy-epoll-iocp; meson setup builddir 2>&1"
```

Meson will automatically download the `unity` test framework as a wrap subproject on first run.
Expected last lines of output:
```
Build targets in project: 5
Found ninja-1.13.0 at C:/Users/jorge/.local/bin/ninja.EXE
```

### 2b. Compile — run in MSYS2 bash

```powershell
$env:PATH = "C:\msys64\usr\bin;" + $env:PATH
bash -c "export PATH=/mingw64/bin:/usr/bin:/c/Users/jorge/.local/bin:`$PATH; cd /d/Master-IA-Dev/06-Bloque6/1-6-10-revproxy-c/proxy-epoll-iocp; meson compile -C builddir 2>&1"
```

Successful compile produces (in `builddir/`):
```
builddir/
  src/proxy.exe            — the reverse proxy
  tests/test_router.exe
  tests/test_balancer.exe
  tests/test_config.exe
  tests/mock_server.exe    — lightweight HTTP backend for testing
```

### 2c. Clean rebuild (if needed)

```powershell
$env:PATH = "C:\msys64\usr\bin;" + $env:PATH
bash -c "export PATH=/mingw64/bin:/usr/bin:/c/Users/jorge/.local/bin:`$PATH; cd /d/Master-IA-Dev/06-Bloque6/1-6-10-revproxy-c/proxy-epoll-iocp; meson setup builddir --wipe; meson compile -C builddir 2>&1"
```

---

## 3. Unit Tests

Run all three suites with a single command — in **PowerShell** (binaries are already built):

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;C:\Users\jorge\.local\bin;' + $env:PATH
cd D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp
meson test -C builddir --verbose
```

Expected output:
```
1/3 unit - proxy:router   OK   0.43s  — Tests:  8 run, 0 failed
2/3 unit - proxy:balancer OK   0.42s  — Tests: 24 run, 0 failed
3/3 unit - proxy:config   OK   0.43s  — Tests: 40 run, 0 failed

Ok: 3   Fail: 0   Total: 72 assertions
```

### Run a single test suite

```powershell
cd D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp
.\builddir\tests\test_router.exe
.\builddir\tests\test_balancer.exe
.\builddir\tests\test_config.exe
```

### What each suite covers

| Suite | Cases | Coverage |
|-------|-------|----------|
| `test_router` | 8 | Exact match, wildcard `*.suffix`, global `*`, precedence, port-stripping |
| `test_balancer` | 24 | Round-robin cycle, single backend, thread-safety (4 threads × 1 000 iterations) |
| `test_config` | 40 | Valid TOML, missing listener, missing route, port 0, backend without port, `[global]` defaults |

---

## 4. Run the Proxy Manually

### 4a. Start backend mock servers — PowerShell (8 separate terminals)

```powershell
cd D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp

# Terminal 2 — api backends
.\builddir\tests\mock_server.exe 9001 backend-api-1
.\builddir\tests\mock_server.exe 9002 backend-api-2

# Terminal 3 — web backends
.\builddir\tests\mock_server.exe 9003 backend-web-1
.\builddir\tests\mock_server.exe 9004 backend-web-2

# Terminal 4 — admin backends
.\builddir\tests\mock_server.exe 9005 backend-admin-1
.\builddir\tests\mock_server.exe 9006 backend-admin-2

# Terminal 5 — wildcard / default backend
.\builddir\tests\mock_server.exe 9000 backend-default
```

### 4b. Start the proxy — PowerShell (Terminal 1)

```powershell
cd D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp
.\builddir\src\proxy.exe --config proxy.toml --log-level debug
```

Expected startup output:
```
2026-06-08T10:00:00.000Z [INFO ] [main] Proxy started: 1 listener(s), 4 route(s)
2026-06-08T10:00:00.001Z [INFO ] [main] Starting 4 worker thread(s)
2026-06-08T10:00:00.002Z [INFO ] [worker-0] Worker 0 started
...
```

### 4c. Verify routing with curl — PowerShell

```powershell
# api.test — round-robin between backend-api-1 and backend-api-2
curl -H "Host: api.test"   http://127.0.0.1:8080/ping
curl -H "Host: api.test"   http://127.0.0.1:8080/ping   # should alternate

# web.test
curl -H "Host: web.test"   http://127.0.0.1:8080/ping

# admin.test
curl -H "Host: admin.test" http://127.0.0.1:8080/ping

# Unknown domain — must return 502
curl -v -H "Host: unknown.domain" http://127.0.0.1:8080/ping
```

### 4d. Verify round-robin (10 requests)

```powershell
for ($i = 0; $i -lt 10; $i++) {
    curl -s -H "Host: api.test" http://127.0.0.1:8080/ping
}
# Output alternates: backend-api-1 / backend-api-2 / backend-api-1 / ...
```

### 4e. (Optional) Add hosts entries for browser testing

Run **PowerShell as Administrator**, then:
```powershell
Add-Content C:\Windows\System32\drivers\etc\hosts "`n127.0.0.1   api.test"
Add-Content C:\Windows\System32\drivers\etc\hosts "127.0.0.1   web.test"
Add-Content C:\Windows\System32\drivers\etc\hosts "127.0.0.1   admin.test"
```

Then access in browser: `http://api.test:8080/`, `http://web.test:8080/`

### 4f. Stop everything

```powershell
# Ctrl+C in the proxy terminal, or from another terminal:
Get-Process proxy,mock_server -ErrorAction SilentlyContinue | Stop-Process -Force
```

---

## 5. Dynamic Config Reload

While the proxy is running:

```powershell
# 1. Edit the config file
notepad D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp\proxy.toml

# 2. Signal the proxy to reload (Windows named event — no restart needed)
$handle = [System.Threading.EventWaitHandle]::OpenExisting("proxy-reload")
$handle.Set()
```

The proxy log will show:
```
2026-06-08T10:01:00.000Z [INFO ] [main] Config reload triggered
2026-06-08T10:01:00.003Z [INFO ] [main] Config reloaded: 3 route(s) → 4 route(s)
```

Active connections are never interrupted. New connections immediately use the updated routing table.

---

## 6. Integration Tests

The integration test script (`tests/run_integration.ps1`) handles setup, execution, and teardown automatically.

```powershell
# Kill any stale processes first
Get-Process proxy,mock_server -ErrorAction SilentlyContinue | Stop-Process -Force

cd D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp\tests
powershell -ExecutionPolicy Bypass -File .\run_integration.ps1 -NoHosts
```

The `-NoHosts` flag skips writing to `C:\Windows\System32\drivers\etc\hosts` (avoids needing Administrator privileges by using `curl -H "Host: ..."` headers instead).

Expected output:
```
PASS [1/9] Single request routed correctly
PASS [2/9] Round-robin across 2 backends (api.test)
PASS [3/9] Round-robin across 2 backends (web.test)
PASS [4/9] Unknown domain returns 502
PASS [5/9] Config reload (named-event)
PASS [6/9] Backend failover (backend down)
PASS [7/9] Large response body (64 KB)
PASS [8/9] Multiple concurrent connections
PASS [9/9] Persistent connections (keep-alive)

9/9 INTEGRATION TESTS PASSED
```

---

## 7. Load Test

The load test script uses native PowerShell + .NET (`Start-Job` + `HttpWebRequest`) — no external tools required.

> **Windows Defender warning:** PowerShell scripts that make rapid HTTP connections to localhost may be flagged. If the script disappears, add an exclusion first:
> ```powershell
> # Run as Administrator
> Add-MpPreference -ExclusionPath "D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp\tests"
> ```

### Run the load test

```powershell
# Kill stale processes and remove any leftover config
Get-Process proxy,mock_server -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp\proxy_loadtest.toml -ErrorAction SilentlyContinue

cd D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp\tests
powershell -ExecutionPolicy Bypass -File .\run_loadtest.ps1 -Requests 500 -Concurrency 20
```

The script automatically:
1. Starts 4 `mock_server.exe` instances on ports 9101–9104
2. Generates `proxy_loadtest.toml` routing `load.test` to those 4 backends
3. Starts `proxy.exe` on port 8091
4. Runs 10 warmup requests
5. Launches 20 parallel workers (`Start-Job`), each making 25 requests
6. Evaluates thresholds and prints results
7. Kills all processes and removes the temp config

Expected results:
```
======= LOAD TEST RESULTS =======
Total sent    : 500
Successful    : 500
Failed        : 0
Time          : ~2.4 s
Throughput    : ~205 req/s
=================================

PASS All 500 requests succeeded
PASS Success rate 100% >= 99%
PASS Throughput >= 100 req/s

ALL LOAD TESTS PASSED
```

### Load test variants

```powershell
# Heavier load
powershell -ExecutionPolicy Bypass -File .\run_loadtest.ps1 -Requests 1000 -Concurrency 20

# Quick smoke test
powershell -ExecutionPolicy Bypass -File .\run_loadtest.ps1 -Requests 100 -Concurrency 10
```

> **Concurrency limit:** Keep `-Concurrency` at 20 or below. The `mock_server.exe` instances are single-threaded; values above ~30 will saturate their `listen()` backlog and cause connection errors — the bottleneck is the mock backend, not the proxy itself.

---

## 8. Expected Test URLs Reference

| URL | Host header | Expected response |
|-----|-------------|-------------------|
| `http://127.0.0.1:8080/` | `api.test` | `backend-api-1` or `backend-api-2` (alternating) |
| `http://127.0.0.1:8080/` | `web.test` | `backend-web-1` or `backend-web-2` |
| `http://127.0.0.1:8080/` | `admin.test` | `backend-admin-1` or `backend-admin-2` |
| `http://127.0.0.1:8080/` | `unknown` | `HTTP/1.1 502 Bad Gateway` |
| `http://api.test:8080/` | *(auto)* | Requires hosts file entry |

---

## 9. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `cc1.exe` crashes during `meson compile` | MSVC env vars active in PowerShell | Always compile via MSYS2 bash (see Section 2b) |
| `meson` not found | PATH missing `.local/bin` | Run the PATH helper from Section 1 |
| Port already in use | Previous run didn't clean up | `Get-Process proxy,mock_server \| Stop-Process -Force` |
| Port 8090 occupied by system process | `WsToastNotification` or similar | Use port 8091 instead (already the load test default) |
| Load test script disappears | Windows Defender false positive | Add exclusion for the `tests/` folder (see Section 7) |
| Integration tests fail on hosts resolution | `/etc/hosts` entries missing | Use `-NoHosts` flag with `run_integration.ps1` (uses `Host:` header) |
| Stack overflow in `test_config` | `Config` struct is ~16 MB | Already fixed: tests use `calloc()`, not stack allocation |
| Proxy hangs after first connection | IOCP auto-reset bug | Already fixed in `platform/iocp.c` (manual reset of wakeup event) |
| Data not forwarded until second event | `FD_WRITE` not fired unless WOULDBLOCK | Already fixed in `src/tunnel.c` (unconditional flush in TUNNELING state) |

---

## 10. Known Limitations (Windows)

1. **p99 latency not measurable with PowerShell.** `HttpWebRequest` adds ~1–5 ms .NET overhead per call, masking the proxy's sub-millisecond latency. Use `wrk` or `ab` from WSL2 for precise p99 measurement.
2. **Mock servers are single-threaded.** Backlog saturates above ~30 concurrent clients. Not a proxy limitation.
3. **TLS not implemented.** The `tls` field in `proxy.toml` is parsed but has no backend implementation. HTTP only.
4. **Linux `epoll` path not exercised on Windows.** `platform/epoll.c` is compiled and tested only on Linux / WSL2.
