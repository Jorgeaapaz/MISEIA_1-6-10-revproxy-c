@~/.claude/prompts/new_functionality_prompt_spec.md

# Add Static HTML Project Description Page

## Role
Act as a Software Developer and Software Architect, expert in C systems programming, web frontend design, and project documentation.

## Context
- Project: High-performance reverse proxy in C using epoll (Linux) and IOCP (Windows)
- Project root: `D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp`
- GitHub URL: https://github.com/Jorgeaapaz/MISEIA_1-6-10-revproxy-c
- GitLab URL: https://gitlab.codecrypto.academy/jorgeaapaz/MISEIA_1-6-10-revproxy-c
- Build system: Meson
- Key source files: `src/main.c`, `src/config.c`, `src/router.c`, `src/balancer.c`, `platform/epoll.c`
- Existing docs: `docs/requirements.md`, `SPEC.md`, `CLAUDE.md`

## Task
Create a single self-contained `index.html` file at the project root that visually describes the proxy project. The page must include links to both the GitHub and GitLab repositories and serve as a public-facing project landing page.

### HTML Project Page Guidelines

1. **File location:** `D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp\index.html`
2. **Self-contained:** All CSS embedded in `<style>` tag, no external CDN dependencies (offline-safe).
3. **Content sections to include:**
   - Project title and one-line description
   - Architecture overview: epoll (Linux) + IOCP (Windows) async I/O model
   - Key features list:
     - Layer 7 reverse proxy
     - Domain-based routing
     - Multiple listen ports
     - Round-robin load balancing per upstream
     - Dynamic configuration reload
     - Built with Meson build system
   - Repository links section with both GitHub and GitLab badges/buttons
   - Technology stack (C, epoll, IOCP, Meson, Unity test framework)
   - Quick start / build instructions (brief)
4. **Repository links (exact URLs):**
   - GitHub: `https://github.com/Jorgeaapaz/MISEIA_1-6-10-revproxy-c`
   - GitLab: `https://gitlab.codecrypto.academy/jorgeaapaz/MISEIA_1-6-10-revproxy-c`
5. **Design:** Clean, dark-themed terminal/systems aesthetic. Use monospace fonts. No JavaScript required.
6. **No external resources:** Do not link to CDN, Google Fonts, or any remote assets.

## Output Format

Single file: `index.html`

```
index.html
├── <head>  — charset, viewport, title, embedded <style>
└── <body>
    ├── <header>   — project name + tagline
    ├── <section>  — architecture description
    ├── <section>  — key features (ul)
    ├── <section>  — repository links (GitHub + GitLab buttons)
    ├── <section>  — tech stack
    ├── <section>  — quick start (code block)
    └── <footer>   — author / license note
```

## Examples and Steps to Follow

1. **Create git branch** before making any changes:
   ```
   git checkout -b feature/001-html-project-page
   ```

2. **Write `index.html`** at project root following the content guidelines above.

3. **Validate locally:** Open `index.html` in a browser and verify:
   - Both repo links open correctly
   - Page renders without external network requests
   - All sections are present and readable

4. **Commit locally:**
   ```
   git add index.html
   git commit -m "feat: add HTML project description page with repo links"
   ```

5. **Push and create Pull Request** using `/git-only-update`.

6. **Accept PR and merge** into remote main branch.

7. **Switch to local main and pull** remote changes.

## Output Checklist and Guardrails

- [ ] `index.html` exists at project root
- [ ] GitHub link `https://github.com/Jorgeaapaz/MISEIA_1-6-10-revproxy-c` is present and correct
- [ ] GitLab link `https://gitlab.codecrypto.academy/jorgeaapaz/MISEIA_1-6-10-revproxy-c` is present and correct
- [ ] No external CSS/JS/font CDN dependencies
- [ ] Page opens correctly offline (file:// protocol)
- [ ] All key features from CLAUDE.md are mentioned
- [ ] Page is visually coherent — no broken layout, no unstyled content
- [ ] File committed on a feature branch and PR created
- [ ] No other source files modified — this is a pure addition
