@~/.claude/prompts/new_functionality_prompt_spec.md

# Add Dockerfile and Deploy Instructions

## Role
Act as a Software Architect and IT Infrastructure Engineer expert in Docker, multi-stage C builds, Traefik v3, and Google Cloud VM deployment.

## Context
- Project: `proxy-epoll-iocp` — C11 reverse proxy
- Non-compliant item: `dc_instrucciones_deploy`
- Evaluation: "Sección de despliegue con pasos verificables (Dockerfile + comando, script de deploy, instrucciones cloud)"
- Current state: `docker-compose.yml` deploys only `index.html` via nginx; no Dockerfile for the proxy binary
- Remote VM: `ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186`
- Deploy directory: `~/MISEIA_1-6-10-revproxy-c`
- Traefik domain: `proxy.deviaaps.com` (wildcard `*.deviaaps.com`, cert resolver `cloudflare`)
- Network: `miseia-net` (external, already running)
- Production env: `docs/compliance/env.production`
- Prerequisite: T7 (GitHub Actions CI) must be green

## Task
Create a multi-stage `Dockerfile` for the proxy binary, add a `proxy-app` service to `docker-compose.yml`, and add a `## Deploy` section to the README with step-by-step cloud deployment instructions.

### Deploy Instructions Guidelines

#### `Dockerfile` (multi-stage, project root)
```dockerfile
# Stage 1 — Build
FROM ubuntu:22.04 AS builder
RUN apt-get update && apt-get install -y \
    meson ninja-build gcc pkg-config git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .
RUN git submodule update --init --recursive
RUN meson setup builddir -Dbuildtype=release -Doptimization=3
RUN meson compile -C builddir

# Stage 2 — Runtime (minimal)
FROM ubuntu:22.04-slim AS runtime
RUN apt-get update && apt-get install -y libgcc-s1 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /build/builddir/proxy /app/proxy
COPY --from=builder /build/proxy.toml /app/proxy.toml

EXPOSE 8080
ENTRYPOINT ["/app/proxy", "--config", "/app/proxy.toml"]
```

#### `docker-compose.yml` — add `proxy-app` service
```yaml
# Add to existing docker-compose.yml services section:
  proxy-app:
    build: .
    image: proxy-epoll-iocp:latest
    container_name: proxy-app
    restart: unless-stopped
    volumes:
      - ./proxy.toml:/app/proxy.toml:ro
    labels:
      - "traefik.enable=true"
      - "traefik.http.routers.proxy-app.rule=Host(`proxy.deviaaps.com`)"
      - "traefik.http.routers.proxy-app.entrypoints=websecure"
      - "traefik.http.routers.proxy-app.tls=true"
      - "traefik.http.routers.proxy-app.tls.certresolver=cloudflare"
      - "traefik.http.services.proxy-app-svc.loadbalancer.server.port=8080"
    networks:
      - miseia-net
```

#### README — add `## Deploy` section
```markdown
## Deploy

### Docker (local)
\`\`\`bash
docker build -t proxy-epoll-iocp .
docker run -p 8080:8080 -v $(pwd)/proxy.toml:/app/proxy.toml proxy-epoll-iocp
\`\`\`

### Google Cloud VM (production)
Prerequisites: SSH key at `C:\ubuntuiso\.ssh\vboxuser`, Docker running on VM.

\`\`\`bash
# 1. Copy files to VM
scp -i C:\ubuntuiso\.ssh\vboxuser \
    Dockerfile docker-compose.yml proxy.toml index.html \
    gcvmuser@34.174.56.186:~/MISEIA_1-6-10-revproxy-c/

# 2. Build and start on VM
ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186 \
    "cd ~/MISEIA_1-6-10-revproxy-c && docker compose up -d --build"
\`\`\`

**Live:** https://revproxy.deviaaps.com (landing page) · https://proxy.deviaaps.com (proxy)
```

## Output Format
```
proxy-epoll-iocp/
├── Dockerfile            ← new (multi-stage)
├── docker-compose.yml    ← updated (add proxy-app service)
├── .dockerignore         ← new
└── README.md             ← add ## Deploy section
```

#### `.dockerignore`
```
builddir/
builddir-cov/
subprojects/unity/
*.exe
*.obj
*.o
.git
docs/
tests/load_results.*
```

## Examples and Steps to Follow

1. `git checkout -b fix/008-deploy-instructions`
2. Write `Dockerfile` at project root (multi-stage, builder + runtime)
3. Write `.dockerignore`
4. Update `docker-compose.yml` to add `proxy-app` service
5. Test local Docker build: `docker build -t proxy-epoll-iocp .`
6. Test local run: `docker run -p 8080:8080 -v proxy.toml:/app/proxy.toml proxy-epoll-iocp`
7. Add `## Deploy` section to README
8. Deploy to VM via SCP + SSH (step in README)
9. `git add Dockerfile .dockerignore docker-compose.yml README.md`
10. `git commit -m "feat: add Dockerfile and cloud deploy instructions"`
11. Push and PR via `/git-only-update`

## Output Checklist and Guardrails

- [ ] `Dockerfile` present at project root (multi-stage)
- [ ] Stage 1 builds proxy binary with Meson release build
- [ ] Stage 2 is minimal runtime image (no build tools)
- [ ] `.dockerignore` excludes build artifacts and subprojects
- [ ] `docker build -t proxy-epoll-iocp .` succeeds locally
- [ ] `docker-compose.yml` has `proxy-app` service with correct Traefik labels for `proxy.deviaaps.com`
- [ ] `proxy-app` service uses `miseia-net` external network
- [ ] `proxy-app` service does NOT publish host ports (Traefik handles 80/443)
- [ ] README `## Deploy` section has exact commands — copy-paste ready
- [ ] README references live URL `https://proxy.deviaaps.com`
