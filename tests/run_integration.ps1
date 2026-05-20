# Integration test script for the reverse proxy (Windows PowerShell)
# REQ-T-04: Steps 1-9
#
# Requirements:
#   - Build directory at ..\builddir (meson compile -C ..\builddir)
#   - curl.exe available (Windows 10+ has built-in curl)
#   - Administrator rights (to modify hosts file)
#
# Usage (from tests\ directory):
#   .\run_integration.ps1 [-BuildDir <path>] [-NoHosts]

param(
    [string]$BuildDir  = (Join-Path $PSScriptRoot "..\builddir"),
    [switch]$NoHosts   = $false
)

$ErrorActionPreference = "Stop"

# ── Paths ─────────────────────────────────────────────────────────────────────
$ProxyBin   = Join-Path $BuildDir "src\proxy.exe"
$MockBin    = Join-Path $BuildDir "tests\mock_server.exe"
$ProxyToml  = Join-Path $PSScriptRoot "..\proxy_integration_test.toml"
$HostsFile  = "C:\Windows\System32\drivers\etc\hosts"
$ProxyPort  = 8080
$FAILURES   = 0

# Backend ports
$Ports = @{
    API_PORT1   = 9001; API_PORT2   = 9002
    WEB_PORT1   = 9003; WEB_PORT2   = 9004
    ADMIN_PORT1 = 9005; ADMIN_PORT2 = 9006
}

# ── Helpers ────────────────────────────────────────────────────────────────────
function Pass($msg)  { Write-Host "PASS $msg" -ForegroundColor Green }
function Fail($msg)  { Write-Host "FAIL $msg" -ForegroundColor Red; $script:FAILURES++ }
function Info($msg)  { Write-Host "INFO $msg" -ForegroundColor Yellow }

$MockProcesses  = @()
$ProxyProcess   = $null

function Cleanup {
    Info "Cleaning up..."
    if ($ProxyProcess -and !$ProxyProcess.HasExited) {
        $ProxyProcess.Kill()
        $ProxyProcess.WaitForExit(3000) | Out-Null
    }
    foreach ($p in $MockProcesses) {
        if (!$p.HasExited) { $p.Kill() }
    }
    Remove-Item $ProxyToml -ErrorAction SilentlyContinue
    if (!$NoHosts) {
        foreach ($domain in @("api.test","web.test","admin.test")) {
            $content = Get-Content $HostsFile -ErrorAction SilentlyContinue
            if ($content) {
                $content | Where-Object { $_ -notmatch "127\.0\.0\.1\s+$domain" } |
                    Set-Content $HostsFile -ErrorAction SilentlyContinue
            }
        }
    }
}

Register-EngineEvent PowerShell.Exiting -Action { Cleanup } | Out-Null

function Wait-Port($port, $timeout=10) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $timeout) {
        try {
            $tcp = New-Object System.Net.Sockets.TcpClient
            $tcp.Connect("127.0.0.1", $port)
            $tcp.Close()
            return $true
        } catch { Start-Sleep -Milliseconds 200 }
    }
    return $false
}

function Curl-Get($url, $hostHeader=$null) {
    $args = @("-s", "--max-time", "5")
    if ($hostHeader) { $args += @("-H", "Host: $hostHeader") }
    $args += $url
    try {
        $result = & curl.exe @args 2>$null
        return $result
    } catch { return "" }
}

function Curl-Code($url, $hostHeader=$null) {
    $args = @("-s", "-o", "NUL", "-w", "%{http_code}", "--max-time", "5")
    if ($hostHeader) { $args += @("-H", "Host: $hostHeader") }
    $args += $url
    try {
        $result = & curl.exe @args 2>$null
        return $result
    } catch { return "000" }
}

# ── Step 1: Generate proxy_integration_test.toml ──────────────────────────────
Info "Step 1: Generating proxy_integration_test.toml"

@"
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
"@ | Set-Content $ProxyToml

if (Test-Path $ProxyToml) { Pass "proxy_integration_test.toml created" }
else { Fail "Failed to create TOML"; exit 1 }

# ── Step 2: Launch mock servers ───────────────────────────────────────────────
Info "Step 2: Launching mock servers"

if (!(Test-Path $MockBin)) { Fail "mock_server.exe not found at $MockBin"; exit 1 }

$MockData = @(
    @{Port=$Ports.API_PORT1;   Name="api-backend-1"},
    @{Port=$Ports.API_PORT2;   Name="api-backend-2"},
    @{Port=$Ports.WEB_PORT1;   Name="web-backend-1"},
    @{Port=$Ports.WEB_PORT2;   Name="web-backend-2"},
    @{Port=$Ports.ADMIN_PORT1; Name="admin-backend-1"},
    @{Port=$Ports.ADMIN_PORT2; Name="admin-backend-2"}
)

foreach ($m in $MockData) {
    $p = Start-Process -FilePath $MockBin `
                       -ArgumentList $m.Port, $m.Name `
                       -PassThru -WindowStyle Hidden
    $MockProcesses += $p
}

Start-Sleep -Milliseconds 500

foreach ($m in $MockData) {
    if (Wait-Port $m.Port) { Pass "mock server on port $($m.Port) listening" }
    else { Fail "mock server on port $($m.Port) did not start" }
}

$DirectResp = Curl-Get "http://127.0.0.1:$($Ports.API_PORT1)/ping"
if ($DirectResp -eq "api-backend-1") { Pass "direct mock server test" }
else { Fail "direct mock returned: '$DirectResp'" }

# ── Step 3: Modify hosts file ─────────────────────────────────────────────────
if (!$NoHosts) {
    Info "Step 3: Adding hosts file entries"
    $hostsContent = Get-Content $HostsFile -ErrorAction SilentlyContinue
    foreach ($domain in @("api.test","web.test","admin.test")) {
        $entry = "127.0.0.1 $domain"
        if ($hostsContent -notcontains $entry) {
            Add-Content $HostsFile $entry
            Pass "Added: $entry"
        } else {
            Info "$domain already in hosts file"
        }
    }
} else {
    Info "Step 3: Skipping hosts file (using -H flag)"
}

# ── Step 4: Start proxy ───────────────────────────────────────────────────────
Info "Step 4: Starting proxy"

if (!(Test-Path $ProxyBin)) { Fail "proxy.exe not found at $ProxyBin"; exit 1 }

$ProxyProcess = Start-Process -FilePath $ProxyBin `
                               -ArgumentList "--config", $ProxyToml, "--log-level", "debug" `
                               -PassThru -WindowStyle Hidden

if (Wait-Port $ProxyPort) { Pass "proxy listening on port $ProxyPort" }
else { Fail "proxy did not start"; exit 1 }

Start-Sleep -Milliseconds 300

# ── Step 5: Basic routing ──────────────────────────────────────────────────────
Info "Step 5: Testing basic routing"

$Resp = Curl-Get "http://127.0.0.1:${ProxyPort}/ping" "api.test"
if ($Resp -in @("api-backend-1","api-backend-2")) { Pass "api.test routed: '$Resp'" }
else { Fail "api.test returned: '$Resp'" }

$Resp = Curl-Get "http://127.0.0.1:${ProxyPort}/ping" "web.test"
if ($Resp -in @("web-backend-1","web-backend-2")) { Pass "web.test routed: '$Resp'" }
else { Fail "web.test returned: '$Resp'" }

$Resp = Curl-Get "http://127.0.0.1:${ProxyPort}/ping" "admin.test"
if ($Resp -in @("admin-backend-1","admin-backend-2")) { Pass "admin.test routed: '$Resp'" }
else { Fail "admin.test returned: '$Resp'" }

# ── Step 6: 502 for unknown domain ────────────────────────────────────────────
Info "Step 6: Testing 502 for unknown domain"

$Code = Curl-Code "http://127.0.0.1:${ProxyPort}/ping" "unknown.domain.xyz"
if ($Code -eq "502") { Pass "Unknown domain returns 502" }
else { Fail "Unknown domain returned $Code (expected 502)" }

# ── Step 7: Round-robin ────────────────────────────────────────────────────────
Info "Step 7: Testing round-robin (10 requests to api.test)"

$Count1 = 0; $Count2 = 0
for ($i = 0; $i -lt 10; $i++) {
    $Resp = Curl-Get "http://127.0.0.1:${ProxyPort}/ping" "api.test"
    if ($Resp -eq "api-backend-1") { $Count1++ }
    if ($Resp -eq "api-backend-2") { $Count2++ }
}

Info "api-backend-1: $Count1, api-backend-2: $Count2"
if ($Count1 -gt 0 -and $Count2 -gt 0) {
    Pass "Both backends received requests ($Count1/$Count2)"
} else {
    Fail "Round-robin not working: $Count1/$Count2"
}

# ── Step 8: Config reload ──────────────────────────────────────────────────────
Info "Step 8: Testing config reload via named event 'proxy-reload'"

@"
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
"@ | Set-Content $ProxyToml

# Signal reload via the named event
$reloadEvent = [System.Threading.EventWaitHandle]::OpenExisting("proxy-reload") 2>$null
if ($reloadEvent) {
    $reloadEvent.Set() | Out-Null
    $reloadEvent.Close()
} else {
    Info "Named event 'proxy-reload' not found (proxy may not be running yet)"
}

Start-Sleep -Milliseconds 500

$AllOk = $true
for ($i = 0; $i -lt 4; $i++) {
    $Resp = Curl-Get "http://127.0.0.1:${ProxyPort}/ping" "web.test"
    if ($Resp -ne "web-backend-1") { $AllOk = $false; Info "web.test returned '$Resp'" }
}
if ($AllOk) { Pass "Config reload: web.test → single backend" }
else { Fail "Config reload did not take effect" }

# ── Step 9: Stop proxy ─────────────────────────────────────────────────────────
Info "Step 9: Stopping proxy"

if (!$ProxyProcess.HasExited) {
    $ProxyProcess.Kill()
    $ProxyProcess.WaitForExit(5000) | Out-Null
}
Pass "Proxy stopped"

# ── Summary ────────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "========================================"
if ($FAILURES -eq 0) {
    Write-Host "ALL INTEGRATION TESTS PASSED" -ForegroundColor Green
    exit 0
} else {
    Write-Host "$FAILURES INTEGRATION TEST(S) FAILED" -ForegroundColor Red
    exit 1
}
