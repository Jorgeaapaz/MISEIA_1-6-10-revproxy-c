@~/.claude/prompts/new_functionality_prompt_spec.md

# Deploy Proxy Binary Publicly to GCI VM

## Role
Act as a Software Architect and IT Infrastructure Engineer expert in Docker, Traefik v3, and Google Cloud VM deployment.

## Context
- Project: `proxy-epoll-iocp` — C11 reverse proxy
- Non-compliant item: `fn_deploy_publico_accesible`
- Evaluation: "Hay un deploy público accesible (URL) con el proyecto corriendo, documentado en el README"
- Current state: `revproxy.deviaaps.com` serves the static HTML landing page only; proxy binary is NOT running publicly
- Remote VM: `ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186`
- Deploy directory: `~/MISEIA_1-6-10-revproxy-c`
- Network: `miseia-net` (external, already running)
- Target domain: `proxy.deviaaps.com` (Traefik wildcard `*.deviaaps.com`, resolver `cloudflare`)
- Internal proxy port: `8080`
- Traefik service port label: `30001` (per evaluacion-requirements template) → use `8080` (actual proxy listen port)
- Production env: `docs/compliance/env.production`
- Prerequisite: T8b (`Dockerfile` + `docker-compose.yml` updated with `proxy-app` service)

## Task
Deploy the `proxy-app` Docker container to the GCI VM. The proxy should be accessible at `https://proxy.deviaaps.com` via Traefik. The proxy itself will route requests based on Host headers — for the public demo, configure it to proxy traffic to the existing `whoami.deviaaps.com` backend and `revproxy.deviaaps.com`.

Update README with the live URL.

### Public Deploy Guidelines

#### proxy.toml for production (on VM at `~/MISEIA_1-6-10-revproxy-c/proxy.toml`)
```toml
[global]
workers            = 2
connect_timeout_ms = 5000
read_timeout_ms    = 30000
log_level          = "info"
forwarded_for      = true

[[listener]]
port = 8080

[[route]]
domain   = "demo.deviaaps.com"
backends = ["whoami:80"]   # whoami container on miseia-net

[[route]]
domain   = "*"
backends = ["revproxy-web:80"]  # nginx landing page on miseia-net
```

> This config demonstrates the proxy routing to containers on the same Docker network — a realistic internal-network proxy scenario.

#### Deploy steps
```bash
# Step 1 — Create/update proxy.toml on VM
scp -i C:\ubuntuiso\.ssh\vboxuser proxy.toml \
    gcvmuser@34.174.56.186:~/MISEIA_1-6-10-revproxy-c/proxy.toml

# Step 2 — Build and start proxy-app container on VM
ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186 \
    "cd ~/MISEIA_1-6-10-revproxy-c && docker compose up -d --build proxy-app"

# Step 3 — Verify container running
ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186 \
    "docker ps --filter name=proxy-app"

# Step 4 — Smoke test
curl -I https://proxy.deviaaps.com
# Expected: HTTP 200 (landing page via proxy → revproxy-web)

# Step 5 — Test routing to whoami
curl -H "Host: demo.deviaaps.com" https://proxy.deviaaps.com
# Expected: whoami response with IP and headers
```

#### README update
Add to `## Deploy` section (or `## Features Implemented`):

```markdown
### Public Demo

| URL | What it shows |
|---|---|
| https://revproxy.deviaaps.com | Landing page (nginx:alpine, direct Traefik) |
| https://proxy.deviaaps.com | **Proxy running** — routes Host: demo.deviaaps.com → whoami |
```

## Output Format
Files changed:
```
proxy-epoll-iocp/
├── proxy.toml           ← updated for production routes
├── docker-compose.yml   ← proxy-app service (from T8b)
└── README.md            ← add Public Demo table with live URLs
```

Remote VM state after deploy:
```
~/MISEIA_1-6-10-revproxy-c/
├── Dockerfile
├── docker-compose.yml
├── proxy.toml           ← production config (routes to containers on miseia-net)
└── index.html
```

## Examples and Steps to Follow

1. `git checkout -b fix/010-deploy-public`
2. Update `proxy.toml` with production routes (whoami, revproxy-web)
3. Execute deploy steps (SCP + SSH above)
4. Verify `docker ps` shows `proxy-app` Up
5. Run smoke tests (`curl -I https://proxy.deviaaps.com`)
6. Verify routing: `curl -H "Host: demo.deviaaps.com" https://proxy.deviaaps.com` returns whoami response
7. Add Public Demo table to README
8. `git add proxy.toml README.md`
9. `git commit -m "feat: deploy proxy binary to GCI VM at proxy.deviaaps.com"`
10. Push and PR via `/git-only-update`

## Output Checklist and Guardrails

- [ ] `proxy-app` container running on VM (`docker ps` confirms)
- [ ] `curl -I https://proxy.deviaaps.com` returns HTTP 200
- [ ] TLS certificate valid (no browser warning, covered by `*.deviaaps.com` wildcard)
- [ ] `curl -H "Host: demo.deviaaps.com" https://proxy.deviaaps.com` proxies to whoami
- [ ] `proxy-app` is on `miseia-net` (joins existing Traefik network)
- [ ] No host ports published on `proxy-app` (Traefik handles 443)
- [ ] README `## Deploy` section references `https://proxy.deviaaps.com` as live URL
- [ ] Infra compose at `/home/gcvmuser/traefik/docker-compose.yml` NOT modified
- [ ] `proxy.toml` on VM uses internal Docker DNS names (container names, not IPs)
