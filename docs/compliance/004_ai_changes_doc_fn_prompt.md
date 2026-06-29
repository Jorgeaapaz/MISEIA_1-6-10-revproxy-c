@~/.claude/prompts/new_functionality_prompt_spec.md

# Document AI-Generated vs. Human Changes

## Role
Act as a Software Developer and Technical Writer expert in AI-assisted development documentation.

## Context
- Project: `proxy-epoll-iocp` — C11 reverse proxy
- Project root: `D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp`
- Non-compliant item: `dc_cambios_ia_documentados`
- Evaluation: "Si usó IA para generar borradores, documenta qué cambió respecto al borrador (revisión crítica explícita)"
- Existing reference: `RETROSPECTIVA-2026-05-02.md` — session retrospective exists but does not explicitly track AI vs. human delta
- All development was done with Claude Code (Sonnet 4.6) as AI assistant

## Task
Create `docs/AI_COLLABORATION.md` — a structured document that tracks which parts of the project were AI-generated, what was changed/corrected by the developer, and the rationale for deviations from the AI draft.

### AI Changes Documentation Guidelines

Structure of `docs/AI_COLLABORATION.md`:

```markdown
# AI Collaboration Log

## Development Model
This project was built with AI assistance (Claude Code / Sonnet 4.6).
This document tracks where AI drafts were accepted, where they were corrected,
and the reasoning behind each correction.

## Module-by-Module Review

### src/config.c — TOML Parser
- **AI draft:** [brief description of what AI generated]
- **Human changes:** [what was modified and why]
- **Accepted as-is:** [what needed no changes]

### src/router.c — Domain Router
...

### tests/ — Unit Tests
...

### Integration Tests (run_integration.ps1)
...

## Patterns of AI Errors Caught
- [List specific types of bugs found in AI-generated code]

## What AI Did Well
- [List areas where AI drafts were used with minimal changes]

## Critical Review Checklist Applied
- [ ] Memory management (malloc/free balance)
- [ ] Error path completeness (all fds closed on error)
- [ ] Atomic operation memory ordering
- [ ] Thread safety of shared state
- [ ] Windows/Linux platform compatibility
```

Draw content from:
- `RETROSPECTIVA-2026-05-02.md` — session 3 documents specific AI vs. human observations
- Git log: `git log --oneline` to identify commit-by-commit what was added/changed
- Source code comments that document non-obvious choices

## Output Format
```
proxy-epoll-iocp/
└── docs/
    └── AI_COLLABORATION.md    ← new
```
Add a link to `AI_COLLABORATION.md` in the README under a new `## AI Collaboration` section (one paragraph + link).

## Examples and Steps to Follow

1. `git checkout -b fix/004-ai-changes-doc`
2. Read `RETROSPECTIVA-2026-05-02.md` for existing notes
3. Run `git log --oneline` to review commit history
4. Skim each source file for comments marking human corrections
5. Write `docs/AI_COLLABORATION.md` with at least 5 modules reviewed
6. Add `## AI Collaboration` section to README with link
7. `git add docs/AI_COLLABORATION.md README.md`
8. `git commit -m "docs: add AI collaboration log documenting AI vs human changes"`
9. Push and PR via `/git-only-update`

## Output Checklist and Guardrails

- [ ] `docs/AI_COLLABORATION.md` created
- [ ] At least 5 source modules reviewed
- [ ] For each module: AI draft described, human changes listed with reasons
- [ ] "Patterns of AI Errors Caught" section has at least 3 concrete examples
- [ ] "What AI Did Well" section present
- [ ] README links to the document
- [ ] No source code changes — documentation only
