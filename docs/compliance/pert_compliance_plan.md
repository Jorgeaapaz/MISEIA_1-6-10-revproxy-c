# PERT Compliance Plan — proxy-epoll-iocp
**Date:** 2026-06-29  
**Scope:** Remediation of 11 non-compliant items from `compliance_report.md`

---

## PERT Compliance Plan

Dependency-ordered list of remediation tasks. Each task has a reference to its disciplined prompt file.

```
T1 ──────────────────────────────────────────────────────────────────────┐
T2 ─────────────────────────────────────────────┐                        │
T3 ──────────────┐                              │                        │
T4 ──────────────┤                              ▼                        ▼
                 ▼                             T6 ──► T7 ──► T8        T11
                T5                              │      │
                                               T10    T9
```

### T1 — `dc_env_example`: Add `.env.example` and `proxy.toml.example`
- **No dependencies**
- **Prompt:** [`001_env_example_fn_prompt.md`](001_env_example_fn_prompt.md)
- **Rationale:** Fastest win. Unblocks CI (T7) because CI needs documented env vars.
- **Estimate:** 30 min

### T2 — `cq_linter_configurado`: Add `.clang-format` linter configuration
- **No dependencies**
- **Prompt:** [`002_linter_config_fn_prompt.md`](002_linter_config_fn_prompt.md)
- **Rationale:** Fast win. Unblocks CI (T7) — linter must run in pipeline.
- **Estimate:** 1h

### T3 — `dc_decisiones_documentadas`: Document 3+ trade-off decisions in README
- **No dependencies**
- **Prompt:** [`003_decisions_doc_fn_prompt.md`](003_decisions_doc_fn_prompt.md)
- **Rationale:** Independent documentation task. Prerequisite for formal ADRs (T5).
- **Estimate:** 2h

### T4 — `dc_cambios_ia_documentados`: Document AI vs. human changes
- **No dependencies**
- **Prompt:** [`004_ai_changes_doc_fn_prompt.md`](004_ai_changes_doc_fn_prompt.md)
- **Rationale:** Independent documentation task. Can be done in parallel with T1–T3.
- **Estimate:** 1h

### T5 — `dc_adrs_o_decision_log`: Create ADR documents
- **Depends on:** T3 (trade-off decisions must exist before formalizing into ADRs)
- **Prompt:** [`005_adrs_doc_fn_prompt.md`](005_adrs_doc_fn_prompt.md)
- **Rationale:** ADRs formalize the decisions captured in T3 into structured `docs/adr/` files.
- **Estimate:** 2h

### T6 — `cq_cobertura_alta`: Generate and publish coverage report
- **Depends on:** T2 (linter must be clean before measuring coverage meaningfully)
- **Prompt:** [`006_coverage_report_fn_prompt.md`](006_coverage_report_fn_prompt.md)
- **Rationale:** Coverage runs locally with `meson -Db_coverage=true`; report committed to `docs/coverage/`. Unblocks CI (T7) and quantitative benchmark (T9).
- **Estimate:** 2h

### T7 — `cq_ci_funcional` (GitHub): Create GitHub Actions pipeline
- **Depends on:** T1 (env vars documented), T2 (linter config present), T6 (coverage step needed)
- **Prompt:** [`007_ci_github_fn_prompt.md`](007_ci_github_fn_prompt.md)
- **Rationale:** Critical path item. Pipeline must build, lint, test, and report coverage. Unblocks GitLab CI (T8), deploy instructions (T8b), and public deploy (T10).
- **Estimate:** 4h

### T8 — `cq_ci_funcional` (GitLab): Create GitLab CI pipeline
- **Depends on:** T7 (mirror GitHub Actions structure)
- **Prompt:** [`007_ci_gitlab_fn_prompt.md`](007_ci_gitlab_fn_prompt.md)
- **Rationale:** Mirrors GitHub Actions. Required by evaluacion-requirements for GitLab submission.
- **Estimate:** 3h

### T8b — `dc_instrucciones_deploy`: Add Dockerfile and deploy section to README
- **Depends on:** T7 (GitHub Actions must exist before cloud deploy step can reference it)
- **Prompt:** [`008_deploy_instructions_fn_prompt.md`](008_deploy_instructions_fn_prompt.md)
- **Rationale:** Creates `Dockerfile` for the proxy binary, adds deploy section to README, updates docker-compose.yml to include the proxy service alongside nginx.
- **Estimate:** 2h

### T9 — `dc_justificacion_cuantitativa`: Run and publish benchmark results
- **Depends on:** T6 (coverage baseline), T7 (CI must be green before benchmarking)
- **Prompt:** [`009_quantitative_bench_fn_prompt.md`](009_quantitative_bench_fn_prompt.md)
- **Rationale:** Run load tests (`run_loadtest.ps1`) and publish p99 latency numbers in README and `docs/benchmarks/`.
- **Estimate:** 3h

### T10 — `fn_deploy_publico_accesible`: Deploy proxy binary to GCI VM
- **Depends on:** T8b (Dockerfile + deploy instructions must exist)
- **Prompt:** [`010_deploy_public_fn_prompt.md`](010_deploy_public_fn_prompt.md)
- **Rationale:** Final integration step. Containerizes the proxy binary and deploys to `34.174.56.186` behind Traefik at `proxy.deviaaps.com`. Updates README with live URL.
- **Estimate:** 3h

---

## Execution PERT

Tasks ordered by execution sequence. Parallel tracks shown in the same position.

| # | Task | Prompt File | Depends On | Effort | Track |
|---|---|---|---|---|---|
| 1 | T1 — Add `.env.example` + `proxy.toml.example` | `001_env_example_fn_prompt.md` | — | 30 min | A |
| 2 | T2 — Add `.clang-format` linter config | `002_linter_config_fn_prompt.md` | — | 1h | B |
| 3 | T3 — Document trade-off decisions in README | `003_decisions_doc_fn_prompt.md` | — | 2h | C |
| 4 | T4 — Document AI vs. human changes | `004_ai_changes_doc_fn_prompt.md` | — | 1h | D |
| 5 | T5 — Create ADR documents in `docs/adr/` | `005_adrs_doc_fn_prompt.md` | T3 | 2h | C |
| 6 | T6 — Generate and publish coverage report | `006_coverage_report_fn_prompt.md` | T2 | 2h | B |
| 7 | T7 — GitHub Actions CI pipeline | `007_ci_github_fn_prompt.md` | T1, T2, T6 | 4h | A |
| 8 | T8 — GitLab CI pipeline | `007_ci_gitlab_fn_prompt.md` | T7 | 3h | A |
| 9 | T8b — Dockerfile + deploy section | `008_deploy_instructions_fn_prompt.md` | T7 | 2h | B |
| 10 | T9 — Benchmark results in README + docs | `009_quantitative_bench_fn_prompt.md` | T6, T7 | 3h | B |
| 11 | T10 — Deploy proxy to GCI VM (public URL) | `010_deploy_public_fn_prompt.md` | T8b | 3h | A |

**Critical path (longest chain):** T2 → T6 → T7 → T8b → T10 = **13h**  
**Total parallel effort:** ~26.5h  
**Estimated calendar time (2 tracks):** ~16h

---

## Quick-Win Order (execute first)

Execute these 4 tasks first — all independent, total < 4h, high evaluation impact:

1. `T1` — `.env.example` (30 min)
2. `T4` — AI changes doc (1h)
3. `T2` — linter config (1h)
4. `T3` — decisions doc (2h)
