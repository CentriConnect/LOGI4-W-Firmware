param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$BuildDir = "build",
    [string]$Bucket = "centri-logi-firmware-updates",
    [string]$BasePrefix = "logi_4w",
    [string]$AwsRegion = "us-east-1",
    [string]$AwsProfile = "",
    [switch]$Upload,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Resolve-RequiredPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description not found: $Path"
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$semanticVersion = $Version.Trim()
if ($semanticVersion.StartsWith("v")) {
    $semanticVersion = $semanticVersion.Substring(1)
}

if ($semanticVersion -notmatch "^\d+\.\d+\.\d+$") {
    throw "Version must look like 1.6.0 or v1.6.0"
}

$versionTag = "v$semanticVersion"
$versionFolder = "v$($semanticVersion.Replace('.', '_'))"
$firmwareBinaryName = "logi4w_wifi_$versionFolder.bin"
$md5FileName = "$firmwareBinaryName.md5"
$jobFileName = "app_fw_update_job_logi4w_$versionTag.txt"

$resolvedBuildDir = Resolve-RequiredPath -Path (Join-Path $repoRoot $BuildDir) -Description "Build directory"
$appBinary = Resolve-RequiredPath -Path (Join-Path $resolvedBuildDir "LOGI4W.bin") -Description "LOGI4W app binary"

$stageRoot = Join-Path $resolvedBuildDir "release"
$stageDir = Join-Path $stageRoot $versionFolder

if ((Test-Path -LiteralPath $stageDir) -and -not $Force) {
    throw "Release staging directory already exists: $stageDir. Re-run with -Force to replace it."
}

if (Test-Path -LiteralPath $stageDir) {
    $resolvedStageRoot = (Resolve-Path -LiteralPath $stageRoot).Path
    $resolvedStageDir = (Resolve-Path -LiteralPath $stageDir).Path
    if (-not $resolvedStageDir.StartsWith($resolvedStageRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a staging directory outside the release staging root: $resolvedStageDir"
    }

    Remove-Item -LiteralPath $stageDir -Recurse -Force
}

New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

$firmwareBinaryDestination = Join-Path $stageDir $firmwareBinaryName
Copy-Item -LiteralPath $appBinary -Destination $firmwareBinaryDestination

$firmwareBinary = Get-Item -LiteralPath $firmwareBinaryDestination
$firmwareSize = $firmwareBinary.Length
$firmwareMd5 = (Get-FileHash -LiteralPath $firmwareBinaryDestination -Algorithm MD5).Hash.ToLowerInvariant()

$s3Prefix = "$BasePrefix/$versionFolder"
$jobFilePath = Join-Path $stageDir $jobFileName
$md5FilePath = Join-Path $stageDir $md5FileName

$jobDocument = @"
{
  "operation": "app_fw_update",
  "fwversion": "$versionTag",
  "size": $firmwareSize,
  "md5": "$firmwareMd5",
  "location": {
    "protocol": "http:",
    "host": "$Bucket.s3.$AwsRegion.amazonaws.com",
    "path": "$s3Prefix/$firmwareBinaryName"
  }
}
"@

Set-Content -LiteralPath $jobFilePath -Value $jobDocument -Encoding ascii
Set-Content -LiteralPath $md5FilePath -Value "$firmwareMd5  $firmwareBinaryName" -Encoding ascii

Write-Host "Prepared LOGI4W WiFi OTA release $versionTag"
Write-Host "Staged files:"
Get-ChildItem -LiteralPath $stageDir -File | ForEach-Object {
    Write-Host ("  {0} ({1} bytes)" -f $_.Name, $_.Length)
}
Write-Host "MD5: $firmwareMd5"
Write-Host "Destination: s3://$Bucket/$s3Prefix/"
Write-Host "Job document: $jobFilePath"

if (-not $Upload) {
    Write-Host "Dry run only. Re-run with -Upload to upload these files to S3."
    exit 0
}

if (-not (Get-Command aws -ErrorAction SilentlyContinue)) {
    throw "AWS CLI was not found on PATH. Install/configure AWS CLI or run without -Upload."
}

$awsBaseArgs = @()
if ($AwsProfile.Length -gt 0) {
    $awsBaseArgs += @("--profile", $AwsProfile)
}
if ($AwsRegion.Length -gt 0) {
    $awsBaseArgs += @("--region", $AwsRegion)
}

foreach ($file in Get-ChildItem -LiteralPath $stageDir -File) {
    & aws @awsBaseArgs s3 cp $file.FullName "s3://$Bucket/$s3Prefix/$($file.Name)"
}

Write-Host "Uploaded LOGI4W WiFi OTA release $versionTag to s3://$Bucket/$s3Prefix/"
