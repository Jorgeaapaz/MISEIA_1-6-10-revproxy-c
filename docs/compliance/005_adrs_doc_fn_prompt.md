@~/.claude/prompts/new_functionality_prompt_spec.md

# Create Architecture Decision Records (ADRs)

## Role
Act as a Software Architect expert in ADR methodology, C systems design, and technical documentation.

## Context
- Project: `proxy-epoll-iocp` — C11 reverse proxy
- Project root: `D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp`
- Non-compliant item: `dc_adrs_o_decision_log`
- Evaluation: "ADRs o decision log estructurado con contexto/decisión/consecuencias por decisión clave"
- Prerequisite: `003_decisions_doc_fn_prompt.md` must be executed first (trade-offs already written in README)
- Reference: Decisions documented in `README.md § Design Decisions` (after T3 is done)

## Task
Create a `docs/adr/` directory with 5 ADR files following the Nygard ADR format. Each ADR formalizes one architectural decision from the project.

### ADR Guidelines

#### ADR template (Nygard format):
```markdown
# ADR-NNN — <Title>

## Status
Accepted | Superseded by ADR-XXX | Deprecated

## Date
YYYY-MM-DD

## Context
What is the problem or constraint that forced a decision?
What were the forces at play?

## Decision
What was decided? State it positively.

## Consequences
### Positive
- ...
### Negative / Trade-offs
- ...
### Neutral
- ...
```

#### ADRs to create (5 total):

**ADR-001 — epoll Edge-Triggered Mode**
- Context: Need to handle thousands of concurrent connections per worker thread; level-triggered epoll fires repeatedly while data available, wasting wakeups.
- Decision: Use `EPOLLET` (edge-triggered) with mandatory drain-to-EAGAIN loops.
- Consequences: +performance at high concurrency; -complexity (must handle partial reads/EAGAIN in all paths).

**ADR-002 — Lock-Free Round-Robin with C11 `_Atomic int`**
- Context: 32 worker threads selecting backends for the same route simultaneously; mutex would serialize selection.
- Decision: `atomic_fetch_add_explicit(..., memory_order_relaxed)` on per-route counter; modulo by backend count.
- Consequences: +no contention; +single CPU instruction; -requires overflow guard; -not strictly fair in micro-bursts.

**ADR-003 — RCU-Style Atomic Config Reload**
- Context: Need to reload routing table while connections are actively tunneling; stopping the world or reader-writer locks add latency.
- Decision: `_Atomic(SharedState*)` pointer swapped with `atomic_exchange`; workers load pointer once per connection accept.
- Consequences: +zero-overhead hot path; +active connections unaffected; -grace period needed before freeing old state; -harder to debug lifecycle.

**ADR-004 — Thread-Per-Core Worker Model**
- Context: Event-driven servers typically use either single-threaded loops (Node.js) or work-stealing pools; both have contention on shared event queues.
- Decision: Each worker owns its own `epoll` fd; `SO_REUSEPORT` distributes incoming connections across workers at kernel level.
- Consequences: +scales linearly with cores; +no cross-thread event migration; -thundering-herd on accept (mitigated by SO_REUSEPORT); -higher baseline memory.

**ADR-005 — Meson as Build System**
- Context: C project needs cross-platform build (Linux + Windows), dependency management for test framework (Unity) and TOML parser, and coverage support.
- Decision: Meson with `.wrap` files for dependencies; `meson test` for test runner integration.
- Consequences: +modern dependency management; +built-in coverage support; +cross-compilation with cross-file; -less familiar than Make to some evaluators; -requires Python to run.

## Output Format
```
proxy-epoll-iocp/
└── docs/
    └── adr/
        ├── ADR-001-epoll-edge-triggered.md
        ├── ADR-002-lock-free-round-robin.md
        ├── ADR-003-atomic-config-reload.md
        ├── ADR-004-thread-per-core-workers.md
        └── ADR-005-meson-build-system.md
```
Add a `## Architecture Decision Records` section to README with a table listing all 5 ADRs and their status.

## Examples and Steps to Follow

1. `git checkout -b fix/005-adrs` (or add to branch from T3 if not yet merged)
2. Create `docs/adr/` directory
3. Write all 5 ADR files using the template above
4. Add ADR index table to README
5. `git add docs/adr/ README.md`
6. `git commit -m "docs: add 5 Architecture Decision Records (ADRs)"`
7. Push and PR via `/git-only-update`

## Output Checklist and Guardrails

- [ ] `docs/adr/` directory created
- [ ] 5 ADR files present with correct naming `ADR-NNN-*.md`
- [ ] Each ADR has: Status, Date, Context, Decision, Consequences (Positive/Negative/Neutral)
- [ ] Content is project-specific, not generic
- [ ] README has ADR index table
- [ ] No source code changes
