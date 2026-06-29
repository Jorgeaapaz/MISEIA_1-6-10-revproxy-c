@~/.claude/prompts/new_functionality_prompt_spec.md

# Document Architectural Trade-Off Decisions in README

## Role
Act as a Software Architect expert in C systems programming, async I/O, and technical writing.

## Context
- Project: `proxy-epoll-iocp` — C11 reverse proxy
- Project root: `D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp`
- Non-compliant item: `dc_decisiones_documentadas`
- Evaluation: "Sección de 'Decisiones' explicando al menos 2 trade-offs reales (no genéricos)"
- Current state: README has "Design Patterns / Architecture" section describing WHAT patterns were used but not WHY they were chosen over alternatives
- Reference: `SPEC.md`, `RETROSPECTIVA-2026-05-02.md`, source files in `src/`

## Task
Add a `## Design Decisions` section to `README.md` documenting at least 4 real architectural trade-offs with concrete alternatives considered and reasons for choosing the current approach.

### Design Decisions Guidelines

Each decision must follow this structure:
```markdown
### Decision N — <Title>
**Chose:** X  
**Alternatives considered:** Y, Z  
**Why:** Specific technical reason grounded in project constraints.  
**Trade-off:** What we give up by choosing X.
```

Decisions to document (use these — they are factual for this codebase):

**Decision 1 — epoll edge-triggered vs. level-triggered**
- Chose: `EPOLLET` (edge-triggered)
- Alternatives: `EPOLLLT` (level-triggered, the default)
- Why: ET mode fires once per state change — forces the worker to drain the fd completely, eliminating redundant wakeups under high load.
- Trade-off: More complex application logic (must loop until `EAGAIN`); missed events if drain is incomplete.

**Decision 2 — Lock-free round-robin with `_Atomic int` vs. mutex**
- Chose: `atomic_fetch_add` on `_Atomic int rr_index` per route
- Alternatives: `pthread_mutex_t` around index increment; consistent hashing
- Why: A single fetch-add is a hardware CAS instruction — no contention even with 32 worker threads; consistent hashing would require re-hashing on backend changes.
- Trade-off: Modulo overflow must be guarded; not strictly fair under burst (over a short window one backend may get N+1 requests).

**Decision 3 — RCU-style pointer swap for config reload vs. reader-writer lock**
- Chose: `_Atomic(SharedState*)` with `atomic_exchange`; old state freed after grace period
- Alternatives: `pthread_rwlock_t`; stop-the-world reload (restart)
- Why: Zero lock overhead on the hot path (pointer load is a single `atomic_load`); active connections continue unaffected through reload.
- Trade-off: Old config memory held until all references drop — requires reference counting or grace period; harder to reason about lifetime.

**Decision 4 — Thread-per-core worker pool vs. single-threaded event loop**
- Chose: N worker threads each owning their own `epoll` fd
- Alternatives: Single-threaded `epoll` (Node.js style); work-stealing thread pool
- Why: Each core runs independently — no cross-thread fd migration; scales to N CPUs without lock contention on the epoll fd.
- Trade-off: Accept thundering-herd on shared listen sockets (mitigated by `SO_REUSEPORT`); slightly higher memory per thread.

## Output Format
```
README.md  ← add "## Design Decisions" section after "## Design Patterns / Architecture"
```

The section must be at least 400 words with concrete reasoning — not just restatements of what the code does.

## Examples and Steps to Follow

1. `git checkout -b fix/003-decisions-doc`
2. Read current README.md to locate insertion point (after "Design Patterns / Architecture" section)
3. Add `## Design Decisions` section with the 4 decisions above, expanded with project-specific details
4. `git add README.md`
5. `git commit -m "docs: add architectural trade-off decisions section to README"`
6. Push and PR via `/git-only-update`

## Output Checklist and Guardrails

- [ ] Section titled `## Design Decisions` added to README.md
- [ ] Minimum 4 decisions documented
- [ ] Each decision has: Chose / Alternatives / Why / Trade-off
- [ ] Reasoning is project-specific (not generic like "it's faster")
- [ ] No changes to source code
- [ ] Section placed logically in README (after architecture, before Getting Started)
