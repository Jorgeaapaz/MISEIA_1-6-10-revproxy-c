@~/.claude/prompts/new_functionality_prompt_spec.md

# Deploy nginx:alpine Web Server Behind Traefik with SSL

## Role
Act as a Software Architect and IT Infrastructure Engineer, expert in Docker, Traefik v3, Cloudflare DNS, and Linux container deployments on Google Cloud VM.

## Context

### Remote VM
- **SSH**: `ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186`
- **Deploy directory**: `~/MISEIA_1-6-10-revproxy-c`
- **OS**: Ubuntu (Docker already running)

### Existing Infrastructure (do NOT modify)
- **Infra compose file**: `/home/gcvmuser/traefik/docker-compose.yml`
- **Docker network**: `miseia-net` (bridge, already created by infra compose)
- **Traefik version**: v3.3 (running, listens on `:80` and `:443`)
- **TLS certificate resolver**: `cloudflare` (wildcard `*.deviaaps.com` via DNS-01)
- **Traefik rule**: `exposedbydefault=false` → every service **must** set `traefik.enable=true`
- **Traefik network label**: `providers.docker.network=miseia-net`
- **Domain**: `deviaaps.com`
- **Target subdomain for this service**: `revproxy.deviaaps.com`

### Source file
- **`index.html`**: at project root `D:\Master-IA-Dev\06-Bloque6\1-6-10-revproxy-c\proxy-epoll-iocp\index.html`
- Already committed to GitHub: `https://github.com/Jorgeaapaz/MISEIA_1-6-10-revproxy-c`

### Pattern reference (from existing infra compose)
```yaml
# whoami service — canonical label pattern to replicate
whoami:
  image: traefik/whoami
  labels:
    - "traefik.enable=true"
    - "traefik.http.routers.whoami.rule=Host(`whoami.deviaaps.com`)"
    - "traefik.http.routers.whoami.entrypoints=websecure"
    - "traefik.http.routers.whoami.tls=true"
    - "traefik.http.routers.whoami.tls.certresolver=cloudflare"
  networks:
    - miseia-net
```

## Task
Create a `docker-compose.yml` file inside the project that defines a single `nginx:alpine` service. The service:
- Mounts `index.html` (read-only) into the nginx default web root
- Joins the existing `miseia-net` Docker network (declared as external)
- Exposes itself to Traefik on `revproxy.deviaaps.com` with HTTPS via the `cloudflare` cert resolver
- Does **not** publish any host ports directly (Traefik owns 80/443)

Then deploy it to the GCI VM via SSH/SCP.

### nginx Traefik SSL Guidelines

#### 1. Files to create in project root

**`docker-compose.yml`** (new file at project root, separate from infra compose):
```yaml
# nginx:alpine serving index.html behind Traefik
# Network miseia-net is owned by the infra docker-compose — declare as external.
networks:
  miseia-net:
    external: true

services:
  revproxy-web:
    image: nginx:alpine
    container_name: revproxy-web
    restart: unless-stopped
    volumes:
      - ./index.html:/usr/share/nginx/html/index.html:ro
    labels:
      - "traefik.enable=true"
      - "traefik.http.routers.revproxy-web.rule=Host(`revproxy.deviaaps.com`)"
      - "traefik.http.routers.revproxy-web.entrypoints=websecure"
      - "traefik.http.routers.revproxy-web.tls=true"
      - "traefik.http.routers.revproxy-web.tls.certresolver=cloudflare"
      - "traefik.http.services.revproxy-web-svc.loadbalancer.server.port=80"
    networks:
      - miseia-net
```

> **Why a service label for port 80?** nginx:alpine exposes both 80 and 443; Traefik auto-detection would be ambiguous. The explicit service label pins it to port 80 (plain HTTP between container and Traefik — TLS is terminated at Traefik).

#### 2. Deploy steps (execute in order)

```bash
# Step 1 — Create remote directory
ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186 \
    "mkdir -p ~/MISEIA_1-6-10-revproxy-c"

# Step 2 — Copy index.html and docker-compose.yml to VM
scp -i C:\ubuntuiso\.ssh\vboxuser \
    index.html \
    docker-compose.yml \
    gcvmuser@34.174.56.186:~/MISEIA_1-6-10-revproxy-c/

# Step 3 — Start the service on the VM
ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186 \
    "cd ~/MISEIA_1-6-10-revproxy-c && docker compose up -d"

# Step 4 — Verify container is running
ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186 \
    "docker ps --filter name=revproxy-web"

# Step 5 — Check Traefik picked up the route
ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186 \
    "docker logs traefik 2>&1 | tail -20"
```

#### 3. DNS verification
Confirm `revproxy.deviaaps.com` resolves to `34.174.56.186` (should already be covered by the Cloudflare wildcard `*.deviaaps.com` DNS record). If not, add an A record in Cloudflare for `revproxy` → `34.174.56.186`.

#### 4. Smoke test
```bash
# From local machine — expect HTTP 200 with the HTML page
curl -I https://revproxy.deviaaps.com

# From VM — bypass DNS (if DNS not yet propagated)
curl -k --resolve revproxy.deviaaps.com:443:127.0.0.1 https://revproxy.deviaaps.com
```

## Output Format

Two files delivered at project root:

```
proxy-epoll-iocp/
├── docker-compose.yml          ← new: nginx:alpine + Traefik labels
└── index.html                  ← existing (unchanged), copied to VM
```

Remote VM state after deploy:
```
~/MISEIA_1-6-10-revproxy-c/
├── docker-compose.yml
└── index.html
```

Container topology:
```
Internet
  │  HTTPS :443
  ▼
Traefik (container, miseia-net)
  │  HTTP :80 (internal)
  ▼
revproxy-web / nginx:alpine (container, miseia-net)
  │  file mount
  └── index.html
```

## Examples and Steps to Follow

1. **Create git branch**:
   ```
   git checkout -b feature/002-nginx-traefik-ssl
   ```

2. **Write `docker-compose.yml`** at project root (content in Guidelines §1).

3. **Test compose file syntax locally** (Docker must be running):
   ```
   docker compose config
   ```
   Expected: no errors, service `revproxy-web` listed.

4. **Commit**:
   ```
   git add docker-compose.yml
   git commit -m "feat: add nginx:alpine compose service behind Traefik SSL"
   ```

5. **Execute deploy steps** from Guidelines §2 (SSH + SCP).

6. **Run smoke tests** from Guidelines §4.

7. **Validate in browser**: open `https://revproxy.deviaaps.com` — should display the project landing page with valid HTTPS cert (issued by Let's Encrypt, covered by `*.deviaaps.com` wildcard).

8. **Push and create PR** using `/git-only-update`, then merge into main.

## Output Checklist and Guardrails

- [ ] `docker-compose.yml` exists at project root
- [ ] Network declared as `external: true` — does NOT redefine `miseia-net`
- [ ] Service name `revproxy-web` used consistently across router, service, and container labels
- [ ] `traefik.enable=true` label present (required — `exposedbydefault=false` on this Traefik instance)
- [ ] `entrypoints=websecure` (not `web`) — HTTP→HTTPS redirect is handled by Traefik globally
- [ ] `tls.certresolver=cloudflare` matches the resolver name in the infra compose
- [ ] `loadbalancer.server.port=80` explicit (avoids ambiguity on nginx:alpine multi-port image)
- [ ] No host port bindings on `revproxy-web` (Traefik owns 80/443 on the host)
- [ ] `index.html` mounted read-only (`:ro`)
- [ ] `docker compose config` passes with no errors before deploy
- [ ] `curl -I https://revproxy.deviaaps.com` returns HTTP 200
- [ ] TLS certificate is valid (no browser warning)
- [ ] `docker compose up -d` on the VM starts exactly 1 new container (`revproxy-web`)
- [ ] No changes made to the infra compose at `/home/gcvmuser/traefik/docker-compose.yml`
