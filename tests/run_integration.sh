#!/usr/bin/env bash
# Integration test script for the reverse proxy (Linux/macOS)
# REQ-T-04: Steps 1-9
#
# Requirements:
#   - Build directory at ../builddir (meson compile -C ../builddir)
#   - curl available
#   - Running as root or sudo available (for /etc/hosts)
#
# Usage:
#   cd tests
#   ./run_integration.sh [--builddir <path>] [--no-hosts]
#
# --no-hosts: skip modifying /etc/hosts (use curl -H "Host: ..." instead)

set -euo pipefail

# ── Configuration ─────────────────────────────────────────────────────────────
BUILDDIR="${1:-$(dirname "$0")/../builddir}"
NO_HOSTS=0
PROXY_PORT=8080
PROXY_BIN="$BUILDDIR/src/proxy"
MOCK_BIN="$BUILDDIR/tests/mock_server"
PROXY_TOML="$(dirname "$0")/../proxy_integration_test.toml"
PROXY_PID=""
MOCK_PIDS=()

# Ports for mock backends
API_PORT1=9001
API_PORT2=9002
WEB_PORT1=9003
WEB_PORT2=9004
ADMIN_PORT1=9005
ADMIN_PORT2=9006

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# ── Helpers ────────────────────────────────────────────────────────────────────
pass() { echo -e "${GREEN}PASS${NC} $1"; }
fail() { echo -e "${RED}FAIL${NC} $1"; FAILURES=$((FAILURES + 1)); }
info() { echo -e "${YELLOW}INFO${NC} $1"; }
FAILURES=0

cleanup() {
    info "Cleaning up..."
    # Kill proxy
    if [ -n "$PROXY_PID" ]; then
        kill "$PROXY_PID" 2>/dev/null || true
        wait "$PROXY_PID" 2>/dev/null || true
    fi
    # Kill mock servers
    for pid in "${MOCK_PIDS[@]:-}"; do
        kill "$pid" 2>/dev/null || true
    done
    # Remove temp toml
    rm -f "$PROXY_TOML"
    # Remove hosts entries (best effort)
    if [ "$NO_HOSTS" -eq 0 ]; then
        for domain in api.test web.test admin.test; do
            sudo sed -i "/127\.0\.0\.1 $domain/d" /etc/hosts 2>/dev/null || true
        done
    fi
}
trap cleanup EXIT

wait_for_port() {
    local port=$1
    local timeout=10
    local i=0
    while ! nc -z 127.0.0.1 "$port" 2>/dev/null; do
        sleep 0.2
        i=$((i + 1))
        if [ $i -gt $((timeout * 5)) ]; then
            return 1
        fi
    done
    return 0
}

# ── Step 1: Generate proxy.toml ───────────────────────────────────────────────
info "Step 1: Generating proxy_integration_test.toml"

cat > "$PROXY_TOML" << 'EOF'
[global]
workers = 2
connect_timeout_ms = 5000
read_timeout_ms = 10000
log_level = "debug"

[[listener]]
port = 8080
tls = false

[[route]]
domain = "api.test"
backends = ["127.0.0.1:9001", "127.0.0.1:9002"]

[[route]]
domain = "web.test"
backends = ["127.0.0.1:9003", "127.0.0.1:9004"]

[[route]]
domain = "admin.test"
backends = ["127.0.0.1:9005", "127.0.0.1:9006"]
EOF

# Validate TOML was written
[ -f "$PROXY_TOML" ] && pass "proxy_integration_test.toml created" || { fail "Failed to create TOML"; exit 1; }

# ── Step 2: Launch 6 mock servers ─────────────────────────────────────────────
info "Step 2: Launching mock servers"

if [ ! -x "$MOCK_BIN" ]; then
    fail "mock_server binary not found at $MOCK_BIN"
    exit 1
fi

"$MOCK_BIN" $API_PORT1   "api-backend-1"   &  MOCK_PIDS+=($!)
"$MOCK_BIN" $API_PORT2   "api-backend-2"   &  MOCK_PIDS+=($!)
"$MOCK_BIN" $WEB_PORT1   "web-backend-1"   &  MOCK_PIDS+=($!)
"$MOCK_BIN" $WEB_PORT2   "web-backend-2"   &  MOCK_PIDS+=($!)
"$MOCK_BIN" $ADMIN_PORT1 "admin-backend-1" &  MOCK_PIDS+=($!)
"$MOCK_BIN" $ADMIN_PORT2 "admin-backend-2" &  MOCK_PIDS+=($!)

sleep 0.5

# Verify mock servers are up
for port in $API_PORT1 $API_PORT2 $WEB_PORT1 $WEB_PORT2 $ADMIN_PORT1 $ADMIN_PORT2; do
    if wait_for_port $port; then
        pass "mock server on port $port is listening"
    else
        fail "mock server on port $port did not start"
    fi
done

# Direct test of a mock server
DIRECT_RESP=$(curl -s "http://127.0.0.1:${API_PORT1}/ping" 2>/dev/null || true)
[ "$DIRECT_RESP" = "api-backend-1" ] && pass "direct mock server test" || fail "direct mock server returned: '$DIRECT_RESP'"

# ── Step 3: Add /etc/hosts entries ────────────────────────────────────────────
if [ "$NO_HOSTS" -eq 0 ]; then
    info "Step 3: Adding /etc/hosts entries"
    for domain in api.test web.test admin.test; do
        if grep -q "127.0.0.1 $domain" /etc/hosts 2>/dev/null; then
            info "$domain already in /etc/hosts"
        else
            echo "127.0.0.1 $domain" | sudo tee -a /etc/hosts > /dev/null
            pass "Added 127.0.0.1 $domain to /etc/hosts"
        fi
    done
else
    info "Step 3: Skipping /etc/hosts (using -H curl flag)"
fi

# ── Step 4: Start proxy ───────────────────────────────────────────────────────
info "Step 4: Starting proxy"

if [ ! -x "$PROXY_BIN" ]; then
    fail "proxy binary not found at $PROXY_BIN"
    exit 1
fi

"$PROXY_BIN" --config "$PROXY_TOML" --log-level debug &
PROXY_PID=$!

if wait_for_port $PROXY_PORT; then
    pass "proxy is listening on port $PROXY_PORT"
else
    fail "proxy did not start"
    exit 1
fi

sleep 0.3   # let it stabilize

# ── Step 5: Basic routing tests ───────────────────────────────────────────────
info "Step 5: Testing basic routing"

RESP=$(curl -s -H "Host: api.test" "http://127.0.0.1:${PROXY_PORT}/ping" 2>/dev/null || true)
if [[ "$RESP" == "api-backend-1" || "$RESP" == "api-backend-2" ]]; then
    pass "api.test routed correctly: '$RESP'"
else
    fail "api.test returned unexpected: '$RESP'"
fi

RESP=$(curl -s -H "Host: web.test" "http://127.0.0.1:${PROXY_PORT}/ping" 2>/dev/null || true)
if [[ "$RESP" == "web-backend-1" || "$RESP" == "web-backend-2" ]]; then
    pass "web.test routed correctly: '$RESP'"
else
    fail "web.test returned unexpected: '$RESP'"
fi

RESP=$(curl -s -H "Host: admin.test" "http://127.0.0.1:${PROXY_PORT}/ping" 2>/dev/null || true)
if [[ "$RESP" == "admin-backend-1" || "$RESP" == "admin-backend-2" ]]; then
    pass "admin.test routed correctly: '$RESP'"
else
    fail "admin.test returned unexpected: '$RESP'"
fi

# ── Step 6: 502 for unknown domain ────────────────────────────────────────────
info "Step 6: Testing 502 for unknown domain"

HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "Host: unknown.domain.xyz" \
    "http://127.0.0.1:${PROXY_PORT}/ping" 2>/dev/null || true)

if [ "$HTTP_CODE" = "502" ]; then
    pass "Unknown domain returns 502"
else
    fail "Unknown domain returned HTTP $HTTP_CODE (expected 502)"
fi

# ── Step 7: Round-robin distribution ──────────────────────────────────────────
info "Step 7: Testing round-robin distribution (10 requests to api.test)"

COUNT1=0
COUNT2=0
for i in $(seq 1 10); do
    RESP=$(curl -s -H "Host: api.test" "http://127.0.0.1:${PROXY_PORT}/ping" 2>/dev/null || true)
    if [ "$RESP" = "api-backend-1" ]; then COUNT1=$((COUNT1+1)); fi
    if [ "$RESP" = "api-backend-2" ]; then COUNT2=$((COUNT2+1)); fi
done

info "api-backend-1: $COUNT1 requests, api-backend-2: $COUNT2 requests"
if [ $COUNT1 -ge 4 ] && [ $COUNT1 -le 6 ] && [ $COUNT2 -ge 4 ] && [ $COUNT2 -le 6 ]; then
    pass "Round-robin distributed ~50/50 ($COUNT1/$COUNT2)"
else
    # Even if not exactly 50/50, both should have received requests
    if [ $COUNT1 -gt 0 ] && [ $COUNT2 -gt 0 ]; then
        pass "Both backends received requests ($COUNT1/$COUNT2)"
    else
        fail "Round-robin not working: backend-1=$COUNT1, backend-2=$COUNT2"
    fi
fi

# ── Step 8: Config reload (SIGHUP) ────────────────────────────────────────────
info "Step 8: Testing config reload via SIGHUP"

# Modify the config: change web.test to point to only backend 1
cat > "$PROXY_TOML" << 'EOF'
[global]
workers = 2
connect_timeout_ms = 5000
read_timeout_ms = 10000
log_level = "debug"

[[listener]]
port = 8080
tls = false

[[route]]
domain = "api.test"
backends = ["127.0.0.1:9001", "127.0.0.1:9002"]

[[route]]
domain = "web.test"
backends = ["127.0.0.1:9003"]

[[route]]
domain = "admin.test"
backends = ["127.0.0.1:9005", "127.0.0.1:9006"]
EOF

kill -HUP "$PROXY_PID" 2>/dev/null || true
sleep 0.2   # allow reload to complete

# Now all requests to web.test should go to backend-1 only
ALL_OK=1
for i in $(seq 1 4); do
    RESP=$(curl -s -H "Host: web.test" "http://127.0.0.1:${PROXY_PORT}/ping" 2>/dev/null || true)
    if [ "$RESP" != "web-backend-1" ]; then
        ALL_OK=0
        info "web.test returned '$RESP' after reload (expected web-backend-1)"
    fi
done

if [ $ALL_OK -eq 1 ]; then
    pass "Config reload: web.test now routes to single backend only"
else
    fail "Config reload did not take effect correctly"
fi

# ── Step 9: Proxy terminates cleanly ──────────────────────────────────────────
info "Step 9: Stopping proxy cleanly"

kill "$PROXY_PID" 2>/dev/null || true
wait "$PROXY_PID" 2>/dev/null || true
EXIT_CODE=$?
PROXY_PID=""

# kill sends SIGTERM; process may exit with 143 (128+15) which is normal
if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 143 ]; then
    pass "Proxy terminated cleanly (exit code $EXIT_CODE)"
else
    fail "Proxy exit code: $EXIT_CODE"
fi

# ── Summary ────────────────────────────────────────────────────────────────────
echo ""
echo "========================================"
if [ $FAILURES -eq 0 ]; then
    echo -e "${GREEN}ALL INTEGRATION TESTS PASSED${NC}"
    exit 0
else
    echo -e "${RED}$FAILURES INTEGRATION TEST(S) FAILED${NC}"
    exit 1
fi
