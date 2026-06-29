@~/.claude/prompts/new_functionality_prompt_spec.md

# Create GitHub Actions CI/CD Pipeline

## Role
Act as a Software Architect expert in GitHub Actions, C/Meson build pipelines, and Google Cloud VM deployment.

## Context
- Project: `proxy-epoll-iocp` — C11 reverse proxy
- Non-compliant item: `cq_ci_funcional` — no `.github/workflows/` present
- Evaluation: "Pipeline CI configurada que pasa tests + linter en cada push; último build verde"
- GitHub repo: `https://github.com/Jorgeaapaz/MISEIA_1-6-10-revproxy-c`
- Remote VM: `ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186`
- Deploy directory on VM: `~/MISEIA_1-6-10-revproxy-c`
- Traefik domain: `proxy.deviaaps.com` (port 30001, wildcard `*.deviaaps.com`)
- Production env: `docs/compliance/env.production`
- Prerequisites: T1 (`.env.example` done), T2 (`.clang-format` done), T6 (coverage done)

## Task
Create `.github/workflows/ci.yml` that:
1. Builds the proxy on Linux (Ubuntu latest) with Meson
2. Runs unit tests (`meson test`)
3. Checks code formatting (`clang-format --dry-run --Werror`)
4. Generates coverage and posts summary
5. On push to `main`: deploys the Docker container to GCI VM at `proxy.deviaaps.com`

Use `/gh-cli` for all GitHub secrets management.

### GitHub CI/CD Guidelines

#### `.github/workflows/ci.yml`
```yaml
name: CI

on:
  push:
    branches: [main, 'feature/**']
  pull_request:
    branches: [main]

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y meson ninja-build gcc clang-format lcov

      - name: Configure
        run: meson setup builddir -Dbuildtype=debug -Db_coverage=true

      - name: Build
        run: meson compile -C builddir

      - name: Run tests
        run: meson test -C builddir --print-errorlogs

      - name: Check formatting
        run: |
          find src platform -name '*.c' -o -name '*.h' | \
            xargs clang-format --style=file --dry-run --Werror

      - name: Coverage report
        run: |
          ninja -C builddir coverage-text || true
          cat builddir/meson-logs/coverage.txt || echo "Coverage report not generated"

  deploy:
    needs: build-and-test
    runs-on: ubuntu-latest
    if: github.ref == 'refs/heads/main' && github.event_name == 'push'
    steps:
      - uses: actions/checkout@v4

      - name: Set up SSH
        run: |
          mkdir -p ~/.ssh
          echo "${{ secrets.VM_SSH_PRIVATE_KEY }}" > ~/.ssh/deploy_key
          chmod 600 ~/.ssh/deploy_key
          ssh-keyscan -H ${{ secrets.VM_HOST }} >> ~/.ssh/known_hosts

      - name: Copy files to VM
        run: |
          scp -i ~/.ssh/deploy_key \
            index.html docker-compose.yml \
            gcvmuser@${{ secrets.VM_HOST }}:~/MISEIA_1-6-10-revproxy-c/

      - name: Deploy on VM
        run: |
          ssh -i ~/.ssh/deploy_key gcvmuser@${{ secrets.VM_HOST }} \
            "cd ~/MISEIA_1-6-10-revproxy-c && docker compose pull && docker compose up -d"
```

#### GitHub Secrets to configure (use `/gh-cli`):
```bash
# SSH private key for VM access
gh secret set VM_SSH_PRIVATE_KEY < "C:\ubuntuiso\.ssh\vboxuser"

# VM public IP
gh secret set VM_HOST --body "34.174.56.186"

# Cloudflare token (for Traefik wildcard cert)
gh secret set CF_DNS_API_TOKEN --body "cfat_npXIXQCyNmgUfVmjCIqh5HGFF3H6S7amdomWJCc4e1e362f1"
```

## Output Format
```
proxy-epoll-iocp/
└── .github/
    └── workflows/
        └── ci.yml    ← new
```

## Examples and Steps to Follow

1. `git checkout -b fix/007-ci-github`
2. Create `.github/workflows/` directory
3. Write `ci.yml` as above
4. Configure GitHub secrets using `gh secret set` (see above)
5. Push branch: `git push -u origin fix/007-ci-github`
6. Verify CI run passes in GitHub Actions UI
7. PR and merge via `/git-only-update`

## Output Checklist and Guardrails

- [ ] `.github/workflows/ci.yml` present
- [ ] `build-and-test` job: installs deps, configures, builds, tests, checks format
- [ ] `deploy` job: only runs on `main` push, after `build-and-test` passes
- [ ] SSH key secret `VM_SSH_PRIVATE_KEY` configured via `gh secret set`
- [ ] `VM_HOST` secret set to `34.174.56.186`
- [ ] CI run is green (last build passes)
- [ ] `meson test` step exits 0 (all unit tests pass)
- [ ] `clang-format --dry-run --Werror` exits 0 (format check passes)
- [ ] Deploy job SSHes to VM and runs `docker compose up -d`
- [ ] No secrets hardcoded in workflow YAML
