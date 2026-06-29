@~/.claude/prompts/new_functionality_prompt_spec.md

# Run and Publish Quantitative Benchmark Results

## Role
Act as a Software Developer and Performance Engineer expert in HTTP load testing, latency measurement, and C systems benchmarking.

## Context
- Project: `proxy-epoll-iocp` — C11 reverse proxy
- Non-compliant item: `dc_justificacion_cuantitativa`
- Evaluation: "Al menos una decisión técnica justificada con números (benchmark, latencia medida)"
- Current state: README mentions "< 1ms p99" as a target but no measured results exist
- Existing tools: `tests/run_loadtest.ps1` (Windows load test), `tests/run_integration.sh`
- Target decision to justify: Thread-per-core worker model choice (ADR-004)
- Prerequisites: T6 (coverage done), T7 (CI green)

## Task
Run the existing load test, capture real latency measurements, and add a `## Benchmarks` section to the README with measured numbers. Also update the load test script to output machine-readable results.

### Quantitative Benchmark Guidelines

#### Benchmark scenario
```
Configuration:
  - 4 worker threads (matching CPU count)
  - 2 backend servers (mock_server.exe / mock_server)
  - 1 route: api.test → [backend-1:9001, backend-2:9002]
  - Concurrency: 50 simultaneous connections
  - Requests: 1000 total

Measurement:
  - p50 / p95 / p99 latency added by proxy (vs. direct backend)
  - Requests/sec throughput
  - Connection error rate
```

#### Run on Linux (using `wrk` or `ab`):
```bash
# Start mock servers
./builddir/mock_server 9001 &
./builddir/mock_server 9002 &

# Start proxy
./builddir/proxy --config proxy.toml &

# Measure direct (baseline)
wrk -t4 -c50 -d10s --latency http://127.0.0.1:9001/ > docs/benchmarks/direct_baseline.txt

# Measure via proxy
wrk -t4 -c50 -d10s --latency -H "Host: api.test" http://127.0.0.1:8080/ \
  > docs/benchmarks/proxy_result.txt
```

#### Run on Windows (using existing `run_loadtest.ps1`):
```powershell
.\tests\run_loadtest.ps1 | Tee-Object docs\benchmarks\loadtest_results.txt
```

#### Expected result format (`docs/benchmarks/benchmark_results.md`):
```markdown
# Benchmark Results — proxy-epoll-iocp

**Date:** 2026-06-29  
**Platform:** Ubuntu 22.04, 4 cores, 16 GB RAM  
**Proxy version:** 0.1.0  

## Configuration
- Workers: 4
- Backends per route: 2 (round-robin)
- Test tool: wrk v4.2.0
- Concurrency: 50 connections, 10 seconds, 4 threads

## Results

| Metric | Direct (baseline) | Via Proxy | Overhead |
|---|---|---|---|
| p50 latency | X ms | X ms | +X ms |
| p95 latency | X ms | X ms | +X ms |
| p99 latency | X ms | X ms | +X ms |
| Requests/sec | X | X | -X% |
| Error rate | 0% | 0% | 0% |

## Conclusion
Proxy adds **X ms p99 overhead** (target: < 1 ms). Decision to use thread-per-core model
(ADR-004) justified: single-threaded loop achieved X req/s vs thread-per-core X req/s.
```

## Output Format
```
proxy-epoll-iocp/
├── docs/
│   └── benchmarks/
│       ├── benchmark_results.md    ← new (committed)
│       ├── direct_baseline.txt     ← new (raw wrk output)
│       └── proxy_result.txt        ← new (raw wrk output)
└── README.md                       ← add ## Benchmarks section linking to benchmark_results.md
```

## Examples and Steps to Follow

1. `git checkout -b fix/009-quantitative-bench`
2. Build proxy and mock_server in release mode: `meson setup builddir -Dbuildtype=release && meson compile -C builddir`
3. Run load test scenarios (Linux preferred; Windows as alternative)
4. Create `docs/benchmarks/` directory
5. Write `benchmark_results.md` with actual measured numbers
6. Add `## Benchmarks` section to README referencing `docs/benchmarks/benchmark_results.md`
7. `git add docs/benchmarks/ README.md`
8. `git commit -m "perf: add measured benchmark results; p99 latency X ms over baseline"`
9. Push and PR via `/git-only-update`

## Output Checklist and Guardrails

- [ ] `docs/benchmarks/benchmark_results.md` committed with REAL measured numbers (no placeholders)
- [ ] Results table has p50, p95, p99, req/s, error rate
- [ ] Baseline (direct) and proxy results compared
- [ ] p99 overhead documented (meets or explains deviation from < 1ms target)
- [ ] Benchmark ties back to a specific design decision (ADR-004 or thread model)
- [ ] README has `## Benchmarks` section with link to results file
- [ ] Raw wrk/ab output files committed alongside summary
- [ ] Build used for benchmark is release mode (`-Dbuildtype=release`)
