@~/.claude/prompts/new_functionality_prompt_spec.md

# Add .clang-format Linter Configuration

## Role
Act as a Software Developer expert in C11 code quality tooling, clang-format, and Meson build integration.

## Context
- Project: `proxy-epoll-iocp` — C11 reverse proxy
- Project root: `D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp`
- Non-compliant item: `cq_linter_configurado` — no linter/formatter config present
- Evaluation: "Linter o formatter configurado con configuración versionada"
- Compiler flags already set: `-Wall -Wextra -Wpedantic` (see `meson.build`)
- Toolchain: GCC/Clang on Linux, MSVC/Clang on Windows

## Task
Add a `.clang-format` configuration file at the project root and integrate `clang-format` checking into the Meson build via a custom target. Also add a `clang-tidy` config for static analysis.

### Linter Configuration Guidelines

#### `.clang-format` (project root)
Style based on the existing code (K&R-adjacent, 4-space indent, 100-col limit):
```yaml
---
BasedOnStyle: LLVM
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 100
BreakBeforeBraces: Allman
AllowShortFunctionsOnASingleLine: None
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
AlignConsecutiveAssignments: Consecutive
AlignConsecutiveDeclarations: Consecutive
AlignTrailingComments: true
SortIncludes: CaseSensitive
IncludeBlocks: Regroup
SpaceBeforeParens: ControlStatements
PointerAlignment: Right
```

#### `.clang-tidy` (project root)
```yaml
Checks: >
  clang-diagnostic-*,
  clang-analyzer-*,
  bugprone-*,
  cert-*,
  -cert-err33-c,
  misc-*,
  performance-*,
  portability-*
WarningsAsErrors: ''
HeaderFilterRegex: '^src/|^platform/'
CheckOptions:
  - key: bugprone-easily-swappable-parameters.MinimumLength
    value: '3'
```

#### Meson integration — add to `meson.build` (root):
```meson
# ── Formatting check (optional target) ─────────────────────────────────
clang_format = find_program('clang-format', required : false)
if clang_format.found()
  all_c_sources = files(
    'src/main.c', 'src/config.c', 'src/router.c', 'src/balancer.c',
    'src/dispatcher.c', 'src/listener.c', 'src/tunnel.c',
    'src/log.c', 'src/utils.c',
    'platform/epoll.c',
  )
  run_target('format',
    command : [clang_format, '--style=file', '-i', all_c_sources],
  )
  run_target('format-check',
    command : [clang_format, '--style=file', '--dry-run', '--Werror', all_c_sources],
  )
endif
```

#### README update
Add under **Getting Started**:
```markdown
### Code Style
\`\`\`bash
# Format all source files in-place
meson compile -C builddir format

# Check formatting (CI-safe, exits non-zero if reformatting needed)
meson compile -C builddir format-check
\`\`\`
```

## Output Format
```
proxy-epoll-iocp/
├── .clang-format     ← new
├── .clang-tidy       ← new
├── meson.build       ← add format / format-check targets
└── README.md         ← add Code Style section
```

## Examples and Steps to Follow

1. `git checkout -b fix/002-linter-config`
2. Write `.clang-format` at project root
3. Write `.clang-tidy` at project root
4. Add `format` and `format-check` run_targets to root `meson.build`
5. Run `meson compile -C builddir format-check` locally — fix any formatting violations
6. Add Code Style section to README
7. `git add .clang-format .clang-tidy meson.build README.md`
8. `git commit -m "chore: add clang-format and clang-tidy linter configuration"`
9. Push and PR via `/git-only-update`

## Output Checklist and Guardrails

- [ ] `.clang-format` present at project root
- [ ] `.clang-tidy` present at project root
- [ ] `meson compile -C builddir format-check` exits 0 (no formatting violations)
- [ ] `format` and `format-check` targets added to root `meson.build`
- [ ] Existing source files pass `clang-format --dry-run --Werror` without modifications (or are reformatted in the same commit)
- [ ] README documents how to run formatter
- [ ] No changes to test logic or behavior — formatting only
