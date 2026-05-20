param(
    [string]$BuildDir    = (Join-Path $PSScriptRoot "..\builddir"),
    [int]   $Requests    = 500,
    [int]   $Concurrency = 20
)
$ErrorActionPreference = "Stop"

$ProxyBin  = Join-Path $BuildDir "src\proxy.exe"
$MockBin   = Join-Path $BuildDir "tests\mock_server.exe"
$ProxyToml = Join-Path $PSScriptRoot "..\proxy_loadtest.toml"
$ProxyPort = 8091
$FAILURES  = 0
$LogFile   = Join-Path $PSScriptRoot "load_results.log"
"" | Set-Content $LogFile

function Pass($msg) { $l="PASS $msg"; Write-Host $l -ForegroundColor Green;  Add-Content $LogFile $l }
function Fail($msg) { $l="FAIL $msg"; Write-Host $l -ForegroundColor Red;    Add-Content $LogFile $l; $script:FAILURES++ }
function Info($msg) { $l="INFO $msg"; Write-Host $l -ForegroundColor Yellow; Add-Content $LogFile $l }

$MockProcesses = @()
$ProxyProcess  = $null

function Cleanup {
    if ($ProxyProcess -and !$ProxyProcess.HasExited) {
        $ProxyProcess.Kill()
        $ProxyProcess.WaitForExit(3000) | Out-Null
    }
    foreach ($p in $MockProcesses) { if (!$p.HasExited) { $p.Kill() } }
    Remove-Item $ProxyToml -ErrorAction SilentlyContinue
}

function Wait-Port($port, $timeout = 10) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $timeout) {
        try { $c = New-Object Net.Sockets.TcpClient; $c.Connect("127.0.0.1",$port); $c.Close(); return $true }
        catch { Start-Sleep -Milliseconds 200 }
    }
    return $false
}

Info "Starting 4 mock backend servers on ports 9101-9104"
foreach ($port in @(9101,9102,9103,9104)) {
    $MockProcesses += Start-Process -FilePath $MockBin -ArgumentList $port,"backend-$port" -PassThru -WindowStyle Hidden
}
foreach ($port in @(9101,9102,9103,9104)) {
    if (!(Wait-Port $port 5)) { Fail "Mock $port failed to start"; Cleanup; exit 1 }
}
Pass "All mock servers running"

@"
[global]
workers = 2
connect_timeout_ms = 5000
read_timeout_ms = 10000
log_level = "warn"

[[listener]]
port = $ProxyPort
tls = false

[[route]]
domain = "load.test"
backends = ["127.0.0.1:9101", "127.0.0.1:9102", "127.0.0.1:9103", "127.0.0.1:9104"]
"@ | Set-Content $ProxyToml

Info "Starting proxy on port $ProxyPort"
$ProxyProcess = Start-Process -FilePath $ProxyBin -ArgumentList "--config",$ProxyToml -PassThru -WindowStyle Hidden
if (!(Wait-Port $ProxyPort 10)) { Fail "Proxy failed to start"; Cleanup; exit 1 }
Pass "Proxy listening on port $ProxyPort"

Info "Warming up (10 sequential requests)..."
for ($i = 0; $i -lt 10; $i++) {
    try {
        $req = [System.Net.HttpWebRequest]::Create("http://127.0.0.1:$ProxyPort/ping")
        $req.Host = "load.test"; $req.Method = "GET"; $req.Timeout = 5000
        $req.ServicePoint.Expect100Continue = $false
        $r = $req.GetResponse()
        $b = [System.IO.StreamReader]::new($r.GetResponseStream()).ReadToEnd()
        $r.Close()
        if ($b -notmatch "backend") { Fail "Warmup $i bad response: '$b'"; Cleanup; exit 1 }
    } catch { Fail "Warmup $i exception: $($_.Exception.Message)"; Cleanup; exit 1 }
}
Pass "Warmup complete"

$perWorker = [Math]::Ceiling($Requests / $Concurrency)
$url       = "http://127.0.0.1:$ProxyPort/ping"
Info "Running load test: $($Concurrency * $perWorker) requests, $Concurrency concurrent workers ($perWorker req/worker)"

$workerBlock = {
    param([string]$u, [int]$n)
    $ok=0; $fail=0; $errs=@()
    for ($i=0; $i -lt $n; $i++) {
        try {
            $req=[System.Net.HttpWebRequest]::Create($u)
            $req.Host="load.test"; $req.Method="GET"; $req.Timeout=10000
            $req.ServicePoint.Expect100Continue=$false
            $r=$req.GetResponse()
            $b=[System.IO.StreamReader]::new($r.GetResponseStream()).ReadToEnd()
            $r.Close()
            if ($b -match "backend") { $ok++ } else { $fail++; $errs += "bad-body:$b" }
        } catch {
            $fail++
            $errs += $_.Exception.Message.Substring(0,[Math]::Min(60,$_.Exception.Message.Length))
        }
    }
    "$ok $fail" + $(if($errs.Count -gt 0){ "|" + ($errs[0..2] -join ";") } else { "" })
}

$sw   = [System.Diagnostics.Stopwatch]::StartNew()
$jobs = 1..$Concurrency | ForEach-Object { Start-Job -ScriptBlock $workerBlock -ArgumentList $url,$perWorker }
$raw  = $jobs | Wait-Job | Receive-Job
$jobs | Remove-Job
$sw.Stop()

$totalOk=0; $totalFail=0; $topErr=""
foreach ($line in $raw) {
    $parts = $line.Split("|")
    $nums  = $parts[0].Split(" ")
    $totalOk   += [int]$nums[0]
    $totalFail += [int]$nums[1]
    if ($parts.Count -gt 1 -and $topErr -eq "") { $topErr = $parts[1] }
}
$totalSent = $totalOk + $totalFail
$secs      = $sw.Elapsed.TotalSeconds
$rps       = [Math]::Round($totalSent / $secs, 1)

$lines = @(
    "======= LOAD TEST RESULTS =======",
    "Total sent    : $totalSent",
    "Successful    : $totalOk",
    "Failed        : $totalFail",
    "Time          : $([Math]::Round($secs,2)) s",
    "Throughput    : $rps req/s",
    "================================="
)
Write-Host ""
foreach ($l in $lines) { Write-Host $l -ForegroundColor Cyan; Add-Content $LogFile $l }
if ($topErr) {
    $eline = "Sample error  : $topErr"
    Write-Host $eline -ForegroundColor DarkRed; Add-Content $LogFile $eline
}
Write-Host ""

$successRate = [Math]::Round($totalOk * 100.0 / $totalSent, 1)
if ($totalFail -eq 0)      { Pass "All $totalSent requests succeeded"    } else { Fail "$totalFail of $totalSent requests failed" }
if ($successRate -ge 99.0) { Pass "Success rate $successRate% >= 99%"    } else { Fail "Success rate $successRate% < 99%" }
if ($rps -ge 100)           { Pass "Throughput $rps req/s >= 100 req/s"  } else { Fail "Throughput $rps req/s < 100 req/s" }

Cleanup

Write-Host ""
if ($FAILURES -eq 0) {
    $v = "ALL LOAD TESTS PASSED"; Write-Host $v -ForegroundColor Green; Add-Content $LogFile $v; exit 0
} else {
    $v = "$FAILURES LOAD TEST(S) FAILED"; Write-Host $v -ForegroundColor Red; Add-Content $LogFile $v; exit 1
}
