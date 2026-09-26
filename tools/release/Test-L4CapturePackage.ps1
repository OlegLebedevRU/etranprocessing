# Test-L4CapturePackage.ps1 — completeness gate for tools suite packaging (L4C-11).
# Fails when l4capture is missing from build sequence, staging tree, or release components.
[CmdletBinding()]
param(
    [string]$ToolsRoot,
    [switch]$StageOnly
)

$ErrorActionPreference = "Stop"
if (-not $ToolsRoot) { $ToolsRoot = (Resolve-Path "$PSScriptRoot\..").Path }
$ToolsRoot = (Resolve-Path $ToolsRoot).Path
$fail = 0

function Fail([string]$msg) {
    Write-Host "[FAIL] $msg" -ForegroundColor Red
    $script:fail++
}
function Pass([string]$msg) {
    Write-Host "[PASS] $msg" -ForegroundColor Green
}

# 1. build_dist.cmd must build l4capture
$buildDist = Join-Path $ToolsRoot "build_dist.cmd"
if (-not (Test-Path $buildDist)) {
    Fail "build_dist.cmd missing"
} else {
    $bd = Get-Content $buildDist -Raw
    if ($bd -notmatch 'l4capture') {
        Fail "build_dist.cmd does not build l4capture"
    } else {
        Pass "build_dist.cmd includes l4capture"
    }
}

# 2. Staging script must stage l4capture
$stagePs1 = Join-Path $ToolsRoot "release\Build-StagingPayloads.ps1"
if (-not (Test-Path $stagePs1)) {
    Fail "Build-StagingPayloads.ps1 missing"
} else {
    $st = Get-Content $stagePs1 -Raw
    if ($st -notmatch 'l4capture') {
        Fail "Build-StagingPayloads.ps1 does not stage l4capture"
    } else {
        Pass "Build-StagingPayloads.ps1 includes l4capture"
    }
}

# 3. Manifest components must include l4capture
$manPs1 = Join-Path $ToolsRoot "release\New-ReleaseManifest.ps1"
if (-not (Test-Path $manPs1)) {
    Fail "New-ReleaseManifest.ps1 missing"
} else {
    $mn = Get-Content $manPs1 -Raw
    if ($mn -notmatch '"l4capture"') {
        Fail "New-ReleaseManifest.ps1 components missing l4capture"
    } else {
        Pass "New-ReleaseManifest.ps1 components include l4capture"
    }
}

# 3b. The installer must validate, lock-check, swap, and roll back capture.
$unpackSrc = Join-Path $ToolsRoot "l4setup\src\unpack.c"
if (-not (Test-Path $unpackSrc)) {
    Fail "l4setup unpack.c missing"
} else {
    $unpackText = Get-Content $unpackSrc -Raw
    $captureDirEntries = ([regex]::Matches($unpackText, 'L"l4capture"')).Count
    $captureExeEntries = ([regex]::Matches($unpackText, 'L"l4capture\\\\bin\\\\l4capture\.exe"')).Count
    if ($captureDirEntries -ne 2 -or $captureExeEntries -ne 2) {
        Fail "l4setup must include l4capture in swap/rollback and executable checks"
    } else {
        Pass "l4setup swaps, rolls back, and checks l4capture"
    }
}

if (-not $StageOnly) {
    # 4. Staged trees (if present) must contain l4capture\bin\l4capture.exe matching arch
    foreach ($arch in @("x86", "x64")) {
        $exe = Join-Path $ToolsRoot "dist\.stage\$arch\l4capture\bin\l4capture.exe"
        $quality = Join-Path $ToolsRoot "dist\.stage\$arch\l4capture\bin\idle_refresh.ini"
        if (Test-Path $exe) {
            Pass "stage $arch has l4capture\bin\l4capture.exe"
        } else {
            # Stage may be absent before packaging — warn only when stage root exists
            $stageRoot = Join-Path $ToolsRoot "dist\.stage\$arch"
            if (Test-Path $stageRoot) {
                Fail "stage $arch exists but missing l4capture\bin\l4capture.exe"
            } else {
                Write-Host "[SKIP] stage $arch not built yet"
            }
        }
        if (Test-Path $quality) {
            Pass "stage $arch has the accepted capture quality settings"
        } elseif (Test-Path (Join-Path $ToolsRoot "dist\.stage\$arch")) {
            Fail "stage $arch is missing l4capture\bin\idle_refresh.ini"
        }
    }
    # 5. OpenH264 license must ship with l4capture
    $lic = Join-Path $ToolsRoot "l4capture\OPENH264_LICENSE.txt"
    if (Test-Path $lic) { Pass "OPENH264_LICENSE.txt present" }
    else { Fail "l4capture\OPENH264_LICENSE.txt missing" }
}

if ($fail -gt 0) {
    Write-Host "`n$l4capture package completeness: $fail failure(s)" -ForegroundColor Red
    exit 1
}
Write-Host "`n[OK] l4capture package completeness checks passed" -ForegroundColor Green
exit 0
