@~/.claude/prompts/new_functionality_prompt_spec.md

# Add .env.example and proxy.toml.example

## Role
Act as a Software Developer expert in C project documentation and twelve-factor app practices.

## Context
- Project: High-performance reverse proxy in C — `proxy-epoll-iocp`
- Project root: `D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp`
- Non-compliant item: `dc_env_example` — no `.env.example` or config template present
- Evaluation requirement: "`.env.example` (o equivalente) con todas las variables de entorno requeridas listadas, sin valores reales"
- This proxy uses TOML config (`proxy.toml`), not env vars, so the equivalent is a `proxy.toml.example`
- CI/CD will need real env vars (deploy host, SSH key path, etc.) — those go in `.env.example`

## Task
Create two files at the project root:
1. `proxy.toml.example` — documented template of the proxy configuration with all valid keys, ranges, and descriptions; no real IPs
2. `.env.example` — template for CI/CD and deploy environment variables; no real values

### .env.example / proxy.toml.example Guidelines

#### `proxy.toml.example`
```toml
# proxy.toml.example — copy to proxy.toml and fill in your values

[global]
workers            = 0          # 0 = auto-detect CPU count
connect_timeout_ms = 5000       # ms; backend connect deadline
read_timeout_ms    = 30000      # ms; client header read deadline
log_level          = "info"     # trace | debug | info | warn | error
forwarded_for      = true       # inject X-Forwarded-For header

[[listener]]
port = 8080                     # TCP port to listen on (requires root for < 1024)

# Add more [[listener]] blocks for additional ports
# [[listener]]
# port = 443

[[route]]
domain   = "api.example.com"   # exact match (highest precedence)
backends = [
  "127.0.0.1:9001",
  "127.0.0.1:9002",
]

[[route]]
domain   = "*.example.com"     # wildcard subdomain match
backends = ["127.0.0.1:9003"]

[[route]]
domain   = "*"                  # global fallback (lowest precedence)
backends = ["127.0.0.1:9000"]
```

#### `.env.example`
```bash
# .env.example — CI/CD and deploy environment variables
# Copy to .env (never commit .env)

# ── Deploy target ──────────────────────────────────────────────
SSH_HOST=your-vm-ip
SSH_USER=your-ssh-user
DEPLOY_DIR=/home/your-user/MISEIA_1-6-10-revproxy-c

# ── Docker / Traefik ───────────────────────────────────────────
DOMAIN=your-domain.com
TRAEFIK_NETWORK=miseia-net

# ── GCP (for GitHub Actions secrets) ──────────────────────────
GCP_PROJECT=your-gcp-project-id
GCP_ZONE=your-zone
```

#### README update
Add a line under **Getting Started** pointing to `proxy.toml.example`:
```markdown
Copy the example configuration:
\`\`\`bash
cp proxy.toml.example proxy.toml
# Edit proxy.toml to match your backends
\`\`\`
```

## Output Format
```
proxy-epoll-iocp/
├── proxy.toml.example    ← new
├── .env.example          ← new
└── README.md             ← add one paragraph under Getting Started
```

## Examples and Steps to Follow

1. `git checkout -b fix/001-env-example`
2. Write `proxy.toml.example` (all keys from SPEC.md §3, with inline comments explaining valid values and ranges)
3. Write `.env.example` (all CI/CD vars used in GitHub Actions workflow)
4. Add "Copy the example configuration" paragraph to README Getting Started section
5. `git add proxy.toml.example .env.example README.md`
6. `git commit -m "docs: add proxy.toml.example and .env.example templates"`
7. Push and PR via `/git-only-update`

## Output Checklist and Guardrails

- [ ] `proxy.toml.example` present at project root
- [ ] `proxy.toml.example` has ALL keys from `[global]`, `[[listener]]`, and `[[route]]` sections
- [ ] Every key has an inline comment with valid values/range
- [ ] `.env.example` present at project root
- [ ] `.env.example` contains NO real IPs, tokens, or passwords — only variable names and descriptions
- [ ] `proxy.toml` is in `.gitignore` (already is; verify)
- [ ] `.env` is in `.gitignore` (add if not present)
- [ ] README Getting Started references `proxy.toml.example`
