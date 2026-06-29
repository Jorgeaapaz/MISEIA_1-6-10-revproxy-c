@~/.claude/prompts/new_functionality_prompt_spec.md

# Create GitLab CI Pipeline

## Role
Act as a Software Architect expert in GitLab CI/CD, C/Meson pipelines, and automated testing.

## Context
- Project: `proxy-epoll-iocp` — C11 reverse proxy
- Non-compliant item: `cq_ci_funcional` (GitLab mirror)
- GitLab repo: `https://gitlab.codecrypto.academy/jorgeaapaz/MISEIA_1-6-10-revproxy-c`
- Evaluation requirement (from `evaluacion-requirements.md`): GitLab CI pipeline
- Rule: `NODE_ENV=production` only on `npm run build` — NOT as job-level variable (not applicable here; C project, but rule carries over: no global env var pollution)
- Remote VM: `ssh -i C:\ubuntuiso\.ssh\vboxuser gcvmuser@34.174.56.186`
- Deploy directory: `~/MISEIA_1-6-10-revproxy-c`
- Use `/glab` for GitLab CLI operations
- Prerequisite: GitHub Actions CI (T7) must be green first; mirror its job structure

## Task
Create `.gitlab-ci.yml` at the project root with equivalent jobs to the GitHub Actions workflow: build, test, format-check, coverage, and deploy on main push.

### GitLab CI Guidelines

#### `.gitlab-ci.yml`
```yaml
# proxy-epoll-iocp — GitLab CI Pipeline
# Mirrors .github/workflows/ci.yml structure

image: ubuntu:22.04

variables:
  GIT_SUBMODULE_STRATEGY: recursive

stages:
  - build
  - test
  - quality
  - deploy

# ── Stage: build ─────────────────────────────────────────────────────────

build:
  stage: build
  before_script:
    - apt-get update -qq
    - apt-get install -y -qq meson ninja-build gcc pkg-config
  script:
    - meson setup builddir -Dbuildtype=debug
    - meson compile -C builddir
  artifacts:
    paths:
      - builddir/
    expire_in: 1 hour

# ── Stage: test ──────────────────────────────────────────────────────────

unit-tests:
  stage: test
  before_script:
    - apt-get update -qq
    - apt-get install -y -qq meson ninja-build gcc pkg-config
  script:
    - meson setup builddir -Dbuildtype=debug -Db_coverage=true
    - meson compile -C builddir
    - meson test -C builddir --print-errorlogs
  artifacts:
    reports:
      junit: builddir/meson-logs/testlog.junit.xml
    paths:
      - builddir/meson-logs/
    expire_in: 1 week

# ── Stage: quality ───────────────────────────────────────────────────────

format-check:
  stage: quality
  before_script:
    - apt-get update -qq
    - apt-get install -y -qq clang-format
  script:
    - find src platform -name '*.c' -o -name '*.h' |
        xargs clang-format --style=file --dry-run --Werror
  allow_failure: false

coverage:
  stage: quality
  before_script:
    - apt-get update -qq
    - apt-get install -y -qq meson ninja-build gcc pkg-config lcov
  script:
    - meson setup builddir-cov -Dbuildtype=debug -Db_coverage=true
    - meson compile -C builddir-cov
    - meson test -C builddir-cov
    - ninja -C builddir-cov coverage-text || true
  coverage: '/lines\.*:\s*(\d+\.\d+)%/'
  artifacts:
    paths:
      - builddir-cov/meson-logs/coverage.txt
    expire_in: 1 week

# ── Stage: deploy (main only) ─────────────────────────────────────────────

deploy-production:
  stage: deploy
  before_script:
    - apt-get update -qq
    - apt-get install -y -qq openssh-client
    - mkdir -p ~/.ssh
    - echo "$VM_SSH_PRIVATE_KEY" > ~/.ssh/deploy_key
    - chmod 600 ~/.ssh/deploy_key
    - ssh-keyscan -H "$VM_HOST" >> ~/.ssh/known_hosts
  script:
    - scp -i ~/.ssh/deploy_key
        index.html docker-compose.yml
        gcvmuser@$VM_HOST:~/MISEIA_1-6-10-revproxy-c/
    - ssh -i ~/.ssh/deploy_key gcvmuser@$VM_HOST
        "cd ~/MISEIA_1-6-10-revproxy-c && docker compose pull && docker compose up -d"
  environment:
    name: production
    url: https://revproxy.deviaaps.com
  only:
    - main
```

#### GitLab CI Variables to configure (use `/glab`):
```bash
# Set CI variables (masked, protected)
glab variable set VM_SSH_PRIVATE_KEY --value "$(cat C:\ubuntuiso\.ssh\vboxuser)" \
  --masked --protected --repo jorgeaapaz/MISEIA_1-6-10-revproxy-c

glab variable set VM_HOST --value "34.174.56.186" \
  --masked --protected --repo jorgeaapaz/MISEIA_1-6-10-revproxy-c
```

## Output Format
```
proxy-epoll-iocp/
└── .gitlab-ci.yml    ← new
```

## Examples and Steps to Follow

1. Add to branch `fix/007-ci-github` or create `fix/007-ci-gitlab`
2. Write `.gitlab-ci.yml` as above
3. Set CI variables with `glab variable set` (see above)
4. Push to GitLab: `git push gitlab main`
5. Monitor pipeline: `glab ci view` or check `https://gitlab.codecrypto.academy/jorgeaapaz/MISEIA_1-6-10-revproxy-c/-/pipelines`
6. Ensure all stages pass (build → test → quality → deploy on main)

## Output Checklist and Guardrails

- [ ] `.gitlab-ci.yml` present at project root
- [ ] 4 stages defined: build, test, quality, deploy
- [ ] `NODE_ENV` is NOT set as a global/job-level variable (not applicable here; no npm)
- [ ] `deploy-production` job runs only on `main` branch
- [ ] `VM_SSH_PRIVATE_KEY` set as masked, protected CI variable via `glab`
- [ ] `VM_HOST` set as masked, protected CI variable via `glab`
- [ ] `glab ci view` shows all stages green
- [ ] `coverage:` regex pattern extracts percentage from lcov output
- [ ] No secrets hardcoded in `.gitlab-ci.yml`
- [ ] Pipeline completes in < 10 minutes
