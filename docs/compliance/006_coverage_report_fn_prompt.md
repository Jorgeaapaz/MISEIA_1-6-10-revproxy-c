@~/.claude/prompts/new_functionality_prompt_spec.md

# Generate and Publish Test Coverage Report

## Role
Act as a Software Developer expert in C testing, gcov/lcov coverage tooling, and Meson build integration.

## Context
- Project: `proxy-epoll-iocp` — C11 reverse proxy
- Project root: `D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp`
- Non-compliant item: `cq_cobertura_alta` — coverage >60% domain, >40% global; no report published
- Build system: Meson (supports `-Db_coverage=true` natively)
- Test suite: `test_router.c`, `test_balancer.c`, `test_config.c` via `meson test -C builddir`
- SPEC.md §8 already documents coverage commands; they just haven't been run and published
- Platform: Linux (coverage requires gcov; Windows coverage is optional)
- Prerequisite: `002_linter_config_fn_prompt.md` completed (clean code before measuring)

## Task
Run the Meson coverage build, generate an HTML report, commit the summary to `docs/coverage/`, and add a coverage badge/table to the README.

### Coverage Report Guidelines

#### Step 1 — Build with coverage instrumentation
```bash
# Clean any previous builddir
rm -rf builddir-cov

# Configure with coverage
meson setup builddir-cov -Db_coverage=true -Dbuildtype=debug

# Compile
meson compile -C builddir-cov

# Run all tests (generates .gcda files)
meson test -C builddir-cov --print-errorlogs
```

#### Step 2 — Generate HTML and text reports
```bash
# Using ninja coverage targets (Meson sets these up automatically)
ninja -C builddir-cov coverage-html   # → builddir-cov/meson-logs/coveragereport/
ninja -C builddir-cov coverage-text   # → builddir-cov/meson-logs/coverage.txt
```

#### Step 3 — Extract summary and commit
```bash
# Copy summary (not full HTML - too large for git)
mkdir -p docs/coverage
cp builddir-cov/meson-logs/coverage.txt docs/coverage/coverage_summary.txt

# Alternatively, use lcov summary
lcov --summary builddir-cov/meson-logs/coverage.info 2>&1 | tee docs/coverage/coverage_summary.txt
```

#### Step 4 — README badge/table
Add to README under `## Non-Functional Targets`:
```markdown
## Test Coverage

| Module | Lines | Functions | Branches |
|---|---|---|---|
| src/router.c | XX% | XX% | XX% |
| src/balancer.c | XX% | XX% | XX% |
| src/config.c | XX% | XX% | XX% |
| **Total** | **XX%** | **XX%** | **XX%** |

Coverage generated with `meson -Db_coverage=true` + lcov.
Full report: `docs/coverage/coverage_summary.txt`
```

#### Target thresholds
- Domain code (`src/router.c`, `src/balancer.c`, `src/config.c`): **>60% lines**
- Global (all `src/`): **>40% lines**
- If thresholds not met, add missing test cases to `test_router.c`, `test_balancer.c`, or `test_config.c` until they pass

## Output Format
```
proxy-epoll-iocp/
├── docs/
│   └── coverage/
│       └── coverage_summary.txt    ← new (text summary, committed)
└── README.md                       ← add ## Test Coverage section
```

## Examples and Steps to Follow

1. `git checkout -b fix/006-coverage-report`
2. Run coverage build on Linux (steps above)
3. If coverage < threshold: add test cases in `tests/test_*.c` to cover missing branches
4. Rerun until thresholds met
5. Copy `coverage.txt` to `docs/coverage/coverage_summary.txt`
6. Add `## Test Coverage` table to README with actual numbers
7. `git add docs/coverage/ README.md tests/`
8. `git commit -m "test: add coverage report; router 65%, balancer 70%, config 62%"` (use real numbers)
9. Push and PR via `/git-only-update`

## Output Checklist and Guardrails

- [ ] Coverage build completes without errors (`meson test` passes)
- [ ] `docs/coverage/coverage_summary.txt` committed with actual percentages
- [ ] Domain code coverage ≥ 60% lines
- [ ] Global coverage ≥ 40% lines
- [ ] README `## Test Coverage` table populated with real numbers (not placeholders)
- [ ] No HTML report committed (too large — only text summary)
- [ ] `builddir-cov/` in `.gitignore` (add if not present)
- [ ] All existing tests still pass after any new test cases added
