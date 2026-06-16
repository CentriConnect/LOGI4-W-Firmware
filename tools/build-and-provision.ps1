<#
.SYNOPSIS
One-shot factory provision with clear per-step PASS/FAIL: build the LOGI4W
firmware from source, then flash a connected board with its per-board identity
(AWS certs + DeviceID) AND the freshly built firmware. Handles boards that go in
and out of sleep via a catch-and-flash retry loop. Works on a blank ("fresh")
board with a new cert pack.

.DESCRIPTION
Five stages, each reporting PASS / FAIL / WARN, with a summary table and a
machine-readable "RESULT: PASS|FAIL" line plus an exit code (0 = all good):

  1 VALIDATE  tools, cert pack, esptool present
  2 BUILD     ESP-IDF firmware build (incremental; -Clean = fullclean+set-target)
  3 GENERATE  esp_secure_cert + NVS DeviceID images (via provision-board.ps1 -DryRun)
  4 FLASH     all 6 regions in ONE esptool pass, with catch-and-flash retry
  5 VERIFY    BLE scan confirms the board advertises MyPropane-<short> (non-fatal)

Flash region map (offsets fixed by the partition table):
  0xD000  esp_secure_cert (this board's AWS certs)   0x0     bootloader
  0x13000 nvs (this board's DeviceID)                0x8000  partition-table
  0x20000 application (freshly built)                0x19000 ota_data_initial

SLEEP HANDLING: a LOGI4W board in light sleep still enumerates a COM port but
every open fails ("device not functioning"); deep sleep drops the port entirely.
A port that opens successfully means the chip just booted = we are inside the
flash window. Stage 4 polls for that condition and fires esptool the instant the
port is openable; it disarms after a failed attempt against a running app (port
open but reset path refused) until the board cycles, and gives up after
-FlashTimeoutSec with clear guidance. If a board is genuinely asleep, a human
must power-cycle it (pull power ~3 s) - the loop then catches the boot window
automatically.

.PARAMETER Thing
AWS IoT Thing UUID. A folder of this exact name must exist under -CertDir with
device.crt, private.key and aws-root-ca.pem (the cert pack from Nick/CentriConnect).

.PARAMETER Port
COM port (e.g. COM39). If omitted, auto-detects a single ESP32-C6 (USB VID 303A).

.PARAMETER CertDir
Root of per-Thing cert folders. Default: $env:LOGI4W_CERTS else
C:\LOGI-W\LOGI4-W-Certs.

.PARAMETER Clean
From-scratch build (fullclean + set-target + build) instead of incremental.

.PARAMETER FlashTimeoutSec
How long stage 4 keeps trying to catch a flash window. Default 120.

.PARAMETER SkipVerify
Skip the stage-5 BLE check.

.EXAMPLE
.\tools\build-and-provision.ps1 -Thing ed60838a-24cb-42a0-b358-670984739837 -Port COM39
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)] [string]$Thing,
    [string]$Port,
    [string]$CertDir = $(if ($env:LOGI4W_CERTS) { $env:LOGI4W_CERTS } else { 'C:\LOGI-W\LOGI4-W-Certs' }),
    [switch]$Clean,
    [int]$FlashTimeoutSec = 120,
    [switch]$SkipVerify
)

$ErrorActionPreference = 'Stop'
$repo      = Split-Path -Parent $PSScriptRoot
$idfPath   = $(if ($env:IDF_PATH) { $env:IDF_PATH } else { 'C:\Espressif\esp-idf-v5.5.2' })
$provision = Join-Path $PSScriptRoot 'provision-board.ps1'
$verifyPy  = Join-Path $PSScriptRoot 'verify-ble.py'
$short     = $Thing.Substring(0, [Math]::Min(4, $Thing.Length))
$TOTAL     = 5
$results   = New-Object System.Collections.ArrayList

# ---- step reporting -------------------------------------------------------
function Record($n,$name,$status,$note){ [void]$results.Add([pscustomobject]@{ Step=$n; Name=$name; Status=$status; Note=$note }) }
function Pass($n,$name,$note=''){ Write-Host ("[STEP {0}/{1}] {2} : PASS" -f $n,$TOTAL,$name) -ForegroundColor Green; if($note){ Write-Host "           $note" -ForegroundColor DarkGray }; Record $n $name 'PASS' $note }
function Warn($n,$name,$note=''){ Write-Host ("[STEP {0}/{1}] {2} : WARN" -f $n,$TOTAL,$name) -ForegroundColor Yellow; if($note){ Write-Host "           $note" -ForegroundColor DarkYellow }; Record $n $name 'WARN' $note }
function Fail($n,$name,$note=''){ Write-Host ("[STEP {0}/{1}] {2} : FAIL" -f $n,$TOTAL,$name) -ForegroundColor Red; if($note){ Write-Host "           $note" -ForegroundColor Yellow }; Record $n $name 'FAIL' $note }
function Begin-Step($n,$name){ Write-Host ("`n=== [STEP {0}/{1}] {2} ===" -f $n,$TOTAL,$name) -ForegroundColor Cyan }
function Finish($code){
    Write-Host "`n================== SUMMARY ==================" -ForegroundColor Cyan
    foreach($r in $results){
        $c = switch($r.Status){ 'PASS'{'Green'} 'WARN'{'Yellow'} default{'Red'} }
        Write-Host ("  {0}/{1}  {2,-22}  {3}" -f $r.Step,$TOTAL,$r.Name,$r.Status) -ForegroundColor $c
    }
    $verdict = if($code -eq 0){'PASS'}else{'FAIL'}
    $vc      = if($code -eq 0){'Green'}else{'Red'}
    Write-Host "=============================================" -ForegroundColor Cyan
    Write-Host ("RESULT: {0}" -f $verdict) -ForegroundColor $vc
    exit $code
}

# ---- helpers --------------------------------------------------------------
function Resolve-Port($preferred){
    $c = @(Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.DeviceID -match 'VID_303A' -and $_.Name -match '\(COM\d+\)' } |
        ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } })
    if ($preferred) { if ($c -contains $preferred) { return $preferred } else { return $null } }
    if ($c.Count -ge 1) { return $c[0] }
    return $null
}
function Test-PortOpenable($p){
    try {
        $sp = New-Object System.IO.Ports.SerialPort($p, 115200)
        $sp.DtrEnable = $false; $sp.RtsEnable = $false; $sp.ReadTimeout = 200
        $sp.Open(); $sp.Close()
        return $true
    } catch { return $false }
}
function Resolve-CertFile($dir, [string[]]$names, $label){
    foreach ($name in $names) {
        $path = Join-Path $dir $name
        if (Test-Path $path) { return $path }
    }
    throw "Cert pack incomplete: missing $label in $dir (tried: $($names -join ', '))"
}

# =================== STEP 1: VALIDATE ===================
Begin-Step 1 'Validate inputs'
try {
    if (-not (Test-Path (Join-Path $idfPath 'export.bat'))) { throw "ESP-IDF not found at $idfPath (set `$env:IDF_PATH)" }
    if (-not (Test-Path $provision)) { throw "Missing helper: $provision" }
    $thingDir = Join-Path $CertDir $Thing
    $null = Resolve-CertFile $thingDir @('device.crt', "$short-device.crt") 'device certificate'
    $null = Resolve-CertFile $thingDir @('private.key', "$short-private.key") 'private key'
    $null = Resolve-CertFile $thingDir @('aws-root-ca.pem', 'AmazonRootCA1.pem') 'AWS root CA'
    & python -m esptool version *> $null
    if ($LASTEXITCODE -ne 0) { throw "esptool not runnable ('python -m esptool'); pip install esptool" }
    Pass 1 'Validate inputs' "cert pack OK for $Thing; ESP-IDF + esptool present"
} catch { Fail 1 'Validate inputs' $_.Exception.Message; Finish 1 }

# =================== STEP 2: BUILD ===================
Begin-Step 2 "Build firmware ($(if($Clean){'clean'}else{'incremental'}))"
try {
    $env:MSYSTEM = $null; $env:MSYS = $null; $env:MINGW_PREFIX = $null
    $env:IDF_PATH = $idfPath
    $needTarget = $Clean -or -not (Test-Path (Join-Path $repo 'sdkconfig'))
    $steps = @("call `"$idfPath\export.bat`"", "cd /d `"$repo`"")
    if ($Clean)      { $steps += 'idf.py fullclean' }
    if ($needTarget) { $steps += 'idf.py set-target esp32c6' }
    $steps += 'idf.py build'
    & cmd.exe /c ($steps -join ' && ')
    if ($LASTEXITCODE -ne 0) { throw "idf.py build exited $LASTEXITCODE" }
    foreach ($f in 'bootloader\bootloader.bin','partition_table\partition-table.bin','ota_data_initial.bin','LOGI4W.bin') {
        if (-not (Test-Path (Join-Path $repo "build\$f"))) { throw "build did not produce build\$f" }
    }
    Pass 2 'Build firmware' '4 firmware bins present in build\'
} catch { Fail 2 'Build firmware' $_.Exception.Message; Finish 2 }

# =================== STEP 3: GENERATE identity images ===================
Begin-Step 3 'Generate cert + DeviceID images'
try {
    & $provision -Thing $Thing -CertDir $CertDir -DryRun *> $null
    $pbDir = Join-Path $repo "build\per-board\$Thing"
    $cert  = Get-ChildItem -Path $pbDir -Recurse -Filter 'esp_secure_cert.bin' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
    $nvs   = Join-Path $pbDir 'factory_nvs.bin'
    if (-not $cert -or -not (Test-Path $cert)) { throw 'esp_secure_cert.bin not produced' }
    if (-not (Test-Path $nvs))                 { throw 'factory_nvs.bin not produced' }
    Pass 3 'Generate images' "esp_secure_cert + factory_nvs for $Thing"
} catch { Fail 3 'Generate images' $_.Exception.Message; Finish 3 }

# =================== STEP 4: FLASH (catch-and-flash) ===================
Begin-Step 4 'Flash certs + DeviceID + firmware'
$boot = Join-Path $repo 'build\bootloader\bootloader.bin'
$ptab = Join-Path $repo 'build\partition_table\partition-table.bin'
$ota  = Join-Path $repo 'build\ota_data_initial.bin'
$app  = Join-Path $repo 'build\LOGI4W.bin'
$flashArgs = @('-m','esptool','--chip','esp32c6','-p','__PORT__','-b','460800',
    '--before','default_reset','--after','hard_reset','write_flash',
    '--flash_mode','dio','--flash_freq','80m','--flash_size','4MB',
    '0xD000',$cert,'0x13000',$nvs,'0x0',$boot,'0x8000',$ptab,'0x19000',$ota,'0x20000',$app)

$deadline = (Get-Date).AddSeconds($FlashTimeoutSec)
$armed = $true; $flashed = $false; $lastErr = ''; $lastState = ''
function Note($s){ if($s -ne $script:lastState){ Write-Host "           $s" -ForegroundColor DarkGray; $script:lastState = $s } }

while ((Get-Date) -lt $deadline -and -not $flashed) {
    $p = Resolve-Port $Port
    if (-not $p)                       { $armed = $true; Note 'board not present (deep sleep / unplugged) - power-cycle to wake'; Start-Sleep -Milliseconds 800; continue }
    if (-not (Test-PortOpenable $p))   { $armed = $true; Note "$p present but not openable (light sleep) - waiting for boot window"; Start-Sleep -Milliseconds 600; continue }
    if (-not $armed)                   { Note "$p open but last attempt failed - waiting for board to cycle"; Start-Sleep -Milliseconds 600; continue }
    Write-Host "           $p openable (boot window) - flashing all 6 regions..." -ForegroundColor Cyan
    $a = $flashArgs.Clone(); $a[$a.IndexOf('__PORT__')] = $p
    & python @a
    if ($LASTEXITCODE -eq 0) { $flashed = $true; $flashedPort = $p } else { $armed = $false; $lastErr = "esptool exit $LASTEXITCODE" }
}
if ($flashed) {
    Pass 4 'Flash' "all 6 regions written + hash-verified on $flashedPort"
} else {
    Fail 4 'Flash' "no flash window caught in ${FlashTimeoutSec}s. $lastErr. Power-cycle the board (pull power ~3 s) and re-run, or keep it on USB so it stays awake."
    Finish 4
}

# =================== STEP 5: VERIFY (BLE) ===================
Begin-Step 5 "Verify BLE identity (MyPropane-$short)"
if ($SkipVerify) {
    Warn 5 'Verify BLE' 'skipped (-SkipVerify)'
} elseif (-not (Test-Path $verifyPy)) {
    Warn 5 'Verify BLE' "verify-ble.py not found next to this script; flash hashes already verified the write"
} else {
    # The board hard-resets after flashing and runs a ~15-20 s boot-window
    # countdown BEFORE it starts advertising, so wait, then retry the scan.
    Write-Host "           waiting ~15 s for the board to boot and start advertising..." -ForegroundColor DarkGray
    Start-Sleep -Seconds 15
    $seen = $false
    for ($i = 1; $i -le 3 -and -not $seen; $i++) {
        Write-Host "           BLE scan attempt $i/3..." -ForegroundColor DarkGray
        & python $verifyPy ("MyPropane-$short")
        if ($LASTEXITCODE -eq 0) { $seen = $true } elseif ($i -lt 3) { Start-Sleep -Seconds 2 }
    }
    if ($seen) { Pass 5 'Verify BLE' "board advertising MyPropane-$short" }
    else { Warn 5 'Verify BLE' "MyPropane-$short not seen after 3 scans (BT adapter / range / timing?); flash itself PASSED (hashes verified)" }
}

Finish 0
