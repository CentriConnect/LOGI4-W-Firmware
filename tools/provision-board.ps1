<#
.SYNOPSIS
Factory-provision one LOGI4W board with its per-board AWS IoT identity.

.DESCRIPTION
Writes the Thing UUID into the DeviceCfg namespace of the NVS partition and
flashes the TLS cert + private key into the esp_secure_cert partition. The
shared production firmware image is flashed on top.

Same firmware on every board -- identity comes from these two per-board
partitions. Idempotent: re-running overwrites both partitions. WARNING:
overwrites the entire NVS partition, which includes any stored WiFi creds.

.PARAMETER Port
COM port the board is enumerated on (e.g. COM38). Find via Get-PnpDevice.

.PARAMETER Thing
AWS IoT Thing UUID. Must match a folder name inside -CertDir.

.PARAMETER CertDir
Root directory containing per-Thing cert folders. Each folder must contain
`device.crt`, `private.key`, and `aws-root-ca.pem`. Defaults to the
LOGI4W_CERTS env var, falling back to C:\LOGI-W\LOGI4-W-Certs.

.PARAMETER SkipFirmware
Only flash the two per-board partitions; do not flash the application image.

.PARAMETER DryRun
Generate the NVS + esp_secure_cert partition images under build/per-board/<Thing>
but do not flash. Useful when no board is connected.

.EXAMPLE
.\tools\provision-board.ps1 -Port COM38 -Thing 412a5ffe-a8e0-4b4f-b50b-505443837674

.EXAMPLE
.\tools\provision-board.ps1 -Thing ed60838a-24cb-42a0-b358-670984739837 -DryRun
#>
[CmdletBinding(DefaultParameterSetName='Flash')]
param(
    [Parameter(Mandatory=$true, ParameterSetName='Flash')]
    [string]$Port,

    [Parameter(Mandatory=$true)]
    [string]$Thing,

    [string]$CertDir = $(if ($env:LOGI4W_CERTS) { $env:LOGI4W_CERTS } else { 'C:\LOGI-W\LOGI4-W-Certs' }),

    [switch]$SkipFirmware,

    [Parameter(ParameterSetName='Dry')]
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repo       = Split-Path -Parent $PSScriptRoot
$idfPath    = $(if ($env:IDF_PATH) { $env:IDF_PATH } else { 'C:\Espressif\esp-idf-v5.5.2' })
$idfPython  = $(if ($env:IDF_PYTHON_ENV_PATH) { Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe' } else { 'C:\Users\nicol\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' })
$secureTool = Join-Path $repo 'managed_components\espressif__esp_secure_cert_mgr\tools\configure_esp_secure_cert.py'
$nvsGenTool = Join-Path $idfPath 'components\nvs_flash\nvs_partition_generator\nvs_partition_gen.py'
$shortThing = $Thing.Substring(0, [Math]::Min(4, $Thing.Length))

function Resolve-CertFile($dir, [string[]]$names, $label) {
    foreach ($name in $names) {
        $path = Join-Path $dir $name
        if (Test-Path $path) { return $path }
    }
    throw "Missing $label in ${dir}. Tried: $($names -join ', ')"
}

# Tool sanity
foreach ($f in @($idfPython, $secureTool, $nvsGenTool)) {
    if (-not (Test-Path $f)) { throw "Missing tool: $f" }
}

# Cert pack sanity
$thingDir   = Join-Path $CertDir $Thing
if (-not (Test-Path $thingDir)) { throw "Cert directory not found: $thingDir" }
$deviceCert = Resolve-CertFile $thingDir @('device.crt', "$shortThing-device.crt") 'device certificate'
$privateKey = Resolve-CertFile $thingDir @('private.key', "$shortThing-private.key") 'private key'
$rootCa     = Resolve-CertFile $thingDir @('aws-root-ca.pem', 'AmazonRootCA1.pem') 'AWS root CA'
foreach ($f in @($deviceCert, $privateKey, $rootCa)) {
    if (-not (Test-Path $f)) { throw "Missing cert file: $f" }
}

# Per-board working directory for generated artifacts
$workDir = Join-Path $repo "build\per-board\$Thing"
if (Test-Path $workDir) { Remove-Item -Recurse -Force $workDir }
New-Item -ItemType Directory -Path $workDir -Force | Out-Null

# ESP-IDF tools dislike the MSys env vars Git Bash leaves around
$env:MSYSTEM      = $null
$env:MSYS         = $null
$env:MINGW_PREFIX = $null

# configure_esp_secure_cert.py expects IDF_PATH set + nvs_partition_gen importable
$env:IDF_PATH     = $idfPath
$nvsGenDir        = Join-Path $idfPath 'components\nvs_flash\nvs_partition_generator'
$env:PYTHONPATH   = if ($env:PYTHONPATH) { "$nvsGenDir;$env:PYTHONPATH" } else { $nvsGenDir }

# ----------------------------------------------------------------------------
# 1. NVS factory image -- DeviceId + DeviceIdValid blob entries.
# ----------------------------------------------------------------------------
# Firmware reads both via nvs_get_blob (see EspNvsStorage.cpp), so we must
# write them as NVS blob entries. nvs_partition_gen's `hex2bin` encoding
# stores entries as type=blob, which matches.
Write-Host '[1/4] Generating NVS factory image' -ForegroundColor Cyan

$thingBytes = [System.Text.Encoding]::ASCII.GetBytes($Thing) + 0x00
$thingHex   = ($thingBytes | ForEach-Object { $_.ToString('X2') }) -join ''
# DeviceIdValid is uint16 0xBAFD (DEVICE_ID_VALID_FLAG_VALUE), little-endian
$validHex   = 'FDBA'

$csvPath = Join-Path $workDir 'factory_nvs.csv'
@"
key,type,encoding,value
DeviceCfg,namespace,,
DeviceId,data,hex2bin,$thingHex
DeviceIdValid,data,hex2bin,$validHex
"@ | Set-Content -Path $csvPath -Encoding ASCII

$nvsBin = Join-Path $workDir 'factory_nvs.bin'
& $idfPython $nvsGenTool generate $csvPath $nvsBin 0x6000
if ($LASTEXITCODE -ne 0) { throw 'NVS partition generation failed' }
Write-Host "  -> $nvsBin" -ForegroundColor DarkGray

# ----------------------------------------------------------------------------
# 2. esp_secure_cert partition image (TLV format, no DS peripheral)
# ----------------------------------------------------------------------------
Write-Host '[2/4] Generating esp_secure_cert partition image' -ForegroundColor Cyan

# configure_esp_secure_cert.py demands --port even with --skip_flash because
# DS-mode burns efuses. We are NOT using DS, so the arg is just satisfying
# the parser; pass a placeholder when DryRun (the tool only opens the port
# when actually flashing).
$portArg = if ($DryRun) { 'COM_DRYRUN' } else { $Port }

Push-Location $workDir
try {
    & $idfPython $secureTool `
        --private-key $privateKey `
        --device-cert $deviceCert `
        --ca-cert     $rootCa `
        --target_chip esp32c6 `
        --secure_cert_type cust_flash_tlv `
        --priv_key_algo RSA 2048 `
        --port $portArg `
        --skip_flash
    if ($LASTEXITCODE -ne 0) { throw 'esp_secure_cert partition generation failed' }
} finally {
    Pop-Location
}

$secureCertBin = Get-ChildItem -Path $workDir -Recurse -Filter 'esp_secure_cert.bin' |
                 Select-Object -First 1 -ExpandProperty FullName
if (-not $secureCertBin) { throw 'esp_secure_cert.bin was not produced by the tool' }
Write-Host "  -> $secureCertBin" -ForegroundColor DarkGray

if ($DryRun) {
    Write-Host "`n[DRY RUN] Partition images produced under $workDir -- not flashing." -ForegroundColor Yellow
    Get-ChildItem $workDir -Recurse -File | Format-Table FullName, Length -AutoSize | Out-String | Write-Host
    return
}

# ----------------------------------------------------------------------------
# 3. Flash NVS + esp_secure_cert partitions
# ----------------------------------------------------------------------------
Write-Host "[3/4] Flashing per-board partitions to $Port" -ForegroundColor Cyan

$afterFlag = if ($SkipFirmware) { 'hard_reset' } else { 'no_reset' }

& $idfPython -m esptool `
    --chip esp32c6 `
    -p $Port `
    -b 460800 `
    --before default_reset `
    --after $afterFlag `
    write_flash `
    --flash_mode dio `
    --flash_freq 80m `
    --flash_size 4MB `
    0xD000  $secureCertBin `
    0x13000 $nvsBin
if ($LASTEXITCODE -ne 0) { throw 'esptool partition flash failed' }

# ----------------------------------------------------------------------------
# 4. Flash the shared application firmware (unless skipped)
# ----------------------------------------------------------------------------
if ($SkipFirmware) {
    Write-Host '[4/4] Skipped firmware flash (per -SkipFirmware)' -ForegroundColor DarkGray
} else {
    Write-Host '[4/4] Flashing shared application firmware' -ForegroundColor Cyan
    $buildDir = Join-Path $repo 'build'
    foreach ($f in 'bootloader\bootloader.bin', 'partition_table\partition-table.bin', 'ota_data_initial.bin', 'LOGI4W.bin') {
        if (-not (Test-Path (Join-Path $buildDir $f))) {
            throw "Firmware not built. Run build.bat first; missing: $buildDir\$f"
        }
    }
    & $idfPython -m esptool `
        --chip esp32c6 `
        -p $Port `
        -b 460800 `
        --before default_reset `
        --after hard_reset `
        write_flash `
        --flash_mode dio `
        --flash_freq 80m `
        --flash_size 4MB `
        0x0     (Join-Path $buildDir 'bootloader\bootloader.bin') `
        0x8000  (Join-Path $buildDir 'partition_table\partition-table.bin') `
        0x19000 (Join-Path $buildDir 'ota_data_initial.bin') `
        0x20000 (Join-Path $buildDir 'LOGI4W.bin')
    if ($LASTEXITCODE -ne 0) { throw 'Firmware flash failed' }
}

Write-Host "`nBoard on $Port provisioned as Thing $Thing" -ForegroundColor Green
