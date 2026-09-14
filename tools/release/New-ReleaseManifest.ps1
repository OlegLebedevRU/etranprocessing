[CmdletBinding()]
param(
    [string]$Version,
    [string]$DistDir,
    [string]$ToolsRoot,
    [string]$SetupExe,
    [string]$PayloadX86,
    [string]$PayloadX64,
    [switch]$SkipVerifications
)

$ErrorActionPreference = "Stop"

# Helper: Compute SHA-256
function Get-FileSha256([string]$filePath) {
    if (-not (Test-Path $filePath)) {
        throw "File not found for SHA256: $filePath"
    }
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($filePath)
    try {
        $hashBytes = $hasher.ComputeHash($stream)
        return -join ($hashBytes | ForEach-Object { $_.ToString("x2") })
    } finally {
        $stream.Close()
        $hasher.Dispose()
    }
}

# Resolve paths
if (-not $ToolsRoot) {
    $ToolsRoot = (Resolve-Path "$PSScriptRoot\..").Path
} else {
    $ToolsRoot = (Resolve-Path $ToolsRoot).Path
}

if (-not $DistDir) {
    $DistDir = "$ToolsRoot\dist"
}
if (-not (Test-Path $DistDir)) {
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
}

if (-not $Version) {
    $versionFile = "$ToolsRoot\version.txt"
    if (Test-Path $versionFile) {
        $Version = (Get-Content $versionFile -Raw).Trim()
    } else {
        $Version = "1.7.1"
    }
}

if (-not $SetupExe) {
    $SetupExe = "$DistDir\l4setup.exe"
}
if (-not $PayloadX86) {
    if (Test-Path "$ToolsRoot\l4setup\res\payload_x86.bin") {
        $PayloadX86 = "$ToolsRoot\l4setup\res\payload_x86.bin"
    } else {
        $PayloadX86 = "$DistDir\.stage\payload_x86.bin"
    }
}
if (-not $PayloadX64) {
    if (Test-Path "$ToolsRoot\l4setup\res\payload_x64.bin") {
        $PayloadX64 = "$ToolsRoot\l4setup\res\payload_x64.bin"
    } else {
        $PayloadX64 = "$DistDir\.stage\payload_x64.bin"
    }
}

Write-Host "======================================================="
Write-Host " Generating Release Manifest: l4tools v$Version"
Write-Host " Dist Directory: $DistDir"
Write-Host " Setup Binary:   $SetupExe"
Write-Host "======================================================="

if (-not (Test-Path $SetupExe)) {
    throw "Setup executable not found at: $SetupExe"
}
if (-not (Test-Path $PayloadX86)) {
    throw "Payload x86 not found at: $PayloadX86"
}
if (-not (Test-Path $PayloadX64)) {
    throw "Payload x64 not found at: $PayloadX64"
}

$setupSize = (Get-Item $SetupExe).Length
$payloadX86Size = (Get-Item $PayloadX86).Length
$payloadX64Size = (Get-Item $PayloadX64).Length

Write-Host "File Sizes:"
Write-Host ("  - l4setup.exe:    {0:N0} bytes ({1:N2} MB)" -f $setupSize, ($setupSize / 1MB))
Write-Host ("  - payload_x86:    {0:N0} bytes ({1:N2} MB)" -f $payloadX86Size, ($payloadX86Size / 1MB))
Write-Host ("  - payload_x64:    {0:N0} bytes ({1:N2} MB)" -f $payloadX64Size, ($payloadX64Size / 1MB))

# Verifications
if (-not $SkipVerifications) {
    Write-Host "`nRunning Pre-Manifest Verifications..."

    # 1. Size checks: verify FFmpeg is included (compressed > 10 MB, uncompressed > 20 MB)
    if ($payloadX86Size -le 10MB) {
        throw "Validation Failed: payload_x86.bin size ($payloadX86Size bytes) is <= 10 MB (FFmpeg missing?)"
    }
    if ($payloadX64Size -le 10MB) {
        throw "Validation Failed: payload_x64.bin size ($payloadX64Size bytes) is <= 10 MB (FFmpeg missing?)"
    }
    $stageX86 = "$DistDir\.stage\x86"
    $stageX64 = "$DistDir\.stage\x64"
    if (Test-Path $stageX86) {
        $u86 = (Get-ChildItem -Recurse $stageX86 | Measure-Object -Property Length -Sum).Sum
        if ($u86 -le 20MB) {
            throw "Validation Failed: Staged x86 uncompressed size ($u86 bytes) is <= 20 MB (FFmpeg missing?)"
        }
    }
    if (Test-Path $stageX64) {
        $u64 = (Get-ChildItem -Recurse $stageX64 | Measure-Object -Property Length -Sum).Sum
        if ($u64 -le 20MB) {
            throw "Validation Failed: Staged x64 uncompressed size ($u64 bytes) is <= 20 MB (FFmpeg missing?)"
        }
    }
    Write-Host "  [OK] Payload sizes verified (uncompressed > 20 MB, compressed > 10 MB; FFmpeg present)."

    # 2. Version CLI check
    try {
        $verOut = (& $SetupExe --version 2>&1 | Out-String).Trim()
        Write-Host "  [INFO] $SetupExe --version output: $verOut"
        if (-not ($verOut -match [regex]::Escape($Version))) {
            throw "Validation Failed: $SetupExe --version output '$verOut' does not contain expected version '$Version'"
        }
        Write-Host "  [OK] l4setup.exe version matches $Version."
    } catch {
        throw "Validation Failed running '$SetupExe --version': $_"
    }

    # 3. Authenticode check
    $sig = Get-AuthenticodeSignature $SetupExe
    $isSigned = ($sig.Status -eq [System.Management.Automation.SignatureStatus]::Valid)
    if (-not $isSigned) {
        Write-Host "  [INFO] Authenticode Signature: NotSigned (Stage 1 debt acknowledged)."
    } else {
        Write-Host "  [OK] Authenticode Signature: VALID."
    }

    # 4. Resource check (PAYLOAD_X86 and PAYLOAD_X64)
    $peDef = @"
using System;
using System.Runtime.InteropServices;

public class ReleaseResourceChecker {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr LoadLibraryEx(string lpFileName, IntPtr hReservedNull, uint dwFlags);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr FindResource(IntPtr hModule, string lpName, IntPtr lpType);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool FreeLibrary(IntPtr hModule);

    public const uint LOAD_LIBRARY_AS_DATAFILE = 0x00000002;
    public static readonly IntPtr RT_RCDATA = new IntPtr(10);

    public static bool HasRCDATA(string exePath, string resourceName) {
        IntPtr hMod = LoadLibraryEx(exePath, IntPtr.Zero, LOAD_LIBRARY_AS_DATAFILE);
        if (hMod == IntPtr.Zero) return false;
        try {
            IntPtr hRes = FindResource(hMod, resourceName, RT_RCDATA);
            return hRes != IntPtr.Zero;
        } finally {
            FreeLibrary(hMod);
        }
    }
}
"@
    if (-not ([System.Management.Automation.PSTypeName]'ReleaseResourceChecker').Type) {
        Add-Type -TypeDefinition $peDef -Language CSharp
    }

    $hasX86Res = [ReleaseResourceChecker]::HasRCDATA((Resolve-Path $SetupExe).Path, "PAYLOAD_X86")
    $hasX64Res = [ReleaseResourceChecker]::HasRCDATA((Resolve-Path $SetupExe).Path, "PAYLOAD_X64")

    if (-not $hasX86Res) {
        throw "Validation Failed: Resource PAYLOAD_X86 was not found inside $SetupExe"
    }
    if (-not $hasX64Res) {
        throw "Validation Failed: Resource PAYLOAD_X64 was not found inside $SetupExe"
    }
    Write-Host "  [OK] Embedded resources PAYLOAD_X86 and PAYLOAD_X64 confirmed in executable."
} else {
    $sig = Get-AuthenticodeSignature $SetupExe
    $isSigned = ($sig.Status -eq [System.Management.Automation.SignatureStatus]::Valid)
}

# Hashes
Write-Host "`nComputing SHA-256 Hashes..."
$setupSha = Get-FileSha256 $SetupExe
$payloadX86Sha = Get-FileSha256 $PayloadX86
$payloadX64Sha = Get-FileSha256 $PayloadX64

Write-Host "  - l4setup.exe: $setupSha"
Write-Host "  - payload_x86: $payloadX86Sha"
Write-Host "  - payload_x64: $payloadX64Sha"

# Git info
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $gitSha = (& git -C $ToolsRoot rev-parse HEAD 2>$null)
    if ($gitSha) { $gitSha = $gitSha.Trim() } else { $gitSha = "unknown" }
    $statusOut = (& git -C $ToolsRoot status --porcelain 2>$null) | Where-Object { $_ -and $_.Trim() -ne "" }
    $dirty = ($statusOut.Count -gt 0)
} finally {
    $ErrorActionPreference = $prevEAP
}

$builtAt = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")

# Component versions
function Get-ToolVersion([string]$name, [string]$fallback) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        switch ($name) {
            "leo4proxy" {
                $h = "$ToolsRoot\leo4proxy\src\config.h"
                if (Test-Path $h) {
                    $content = Get-Content $h -Raw
                    if ($content -match '#define\s+LEO4_PROXY_VERSION\s+"([^"]+)"') {
                        return $matches[1]
                    }
                }
            }
            "l4superv" {
                $h = "$ToolsRoot\l4superv\src\config.h"
                if (Test-Path $h) {
                    $content = Get-Content $h -Raw
                    if ($content -match '#define\s+L4_SUPERV_VERSION_STR\s+L?"([^"]+)"') {
                        return $matches[1]
                    }
                }
                $cl = "$ToolsRoot\l4superv\CHANGELOG.md"
                if (Test-Path $cl) {
                    $content = Get-Content $cl -Raw
                    if ($content -match '##\s*\[([0-9]+\.[0-9]+\.[0-9]+)\]') {
                        return $matches[1]
                    }
                }
            }
            "l4desk" {
                $h = "$ToolsRoot\l4desk\src\config.h"
                if (Test-Path $h) {
                    $content = Get-Content $h -Raw
                    if ($content -match '#define\s+L4DESK_VERSION_STR\s+"([^"]+)"') {
                        return $matches[1]
                    }
                }
                $cl = "$ToolsRoot\l4desk\CHANGELOG.md"
                if (Test-Path $cl) {
                    $content = Get-Content $cl -Raw
                    if ($content -match '##\s*\[([0-9]+\.[0-9]+\.[0-9]+)\]') {
                        return $matches[1]
                    }
                }
            }
            "l4pin" {
                $cl = "$ToolsRoot\l4pin\CHANGELOG.md"
                if (Test-Path $cl) {
                    $content = Get-Content $cl -Raw
                    if ($content -match '##\s*\[([0-9]+\.[0-9]+\.[0-9]+)\]') {
                        return $matches[1]
                    }
                }
            }
            "l4con" {
                $h = "$ToolsRoot\l4con\src\config.h"
                if (Test-Path $h) {
                    $content = Get-Content $h -Raw
                    if ($content -match '#define\s+L4CON_APP_VERSION\s+"([^"]+)"') {
                        return $matches[1]
                    }
                }
                $cl = "$ToolsRoot\l4con\CHANGELOG.md"
                if (Test-Path $cl) {
                    $content = Get-Content $cl -Raw
                    if ($content -match '##\s*\[([0-9]+\.[0-9]+\.[0-9]+)\]') {
                        return $matches[1]
                    }
                }
            }
            "mosquitto" {
                $exe = "$ToolsRoot\mosquitto\mosquitto.exe"
                if (Test-Path $exe) {
                    $mOut = (& $exe -h 2>&1 | Out-String)
                    if ($mOut -match 'mosquitto version ([0-9]+\.[0-9]+\.[0-9]+)') {
                        return $matches[1]
                    }
                }
            }
            "ffmpeg" {
                $vf = "$ToolsRoot\ffmpeg\VERSION.txt"
                if (Test-Path $vf) {
                    $content = Get-Content $vf -Raw
                    if ($content -match 'Version:\s*([0-9]+\.[0-9]+)') {
                        return $matches[1]
                    }
                }
            }
        }
        return $fallback
    } finally {
        $ErrorActionPreference = $prev
    }
}

$components = [ordered]@{
    "leo4proxy" = (Get-ToolVersion "leo4proxy" "1.2.0")
    "l4superv"  = (Get-ToolVersion "l4superv" "1.7.2")
    "l4desk"    = (Get-ToolVersion "l4desk" "1.7.2")
    "l4pin"     = (Get-ToolVersion "l4pin" "1.7.2")
    "l4con"     = (Get-ToolVersion "l4con" "1.7.2")
    "l4sql"     = "1.0.0"
    "mosquitto" = (Get-ToolVersion "mosquitto" "2.1.2")
    "ffmpeg"    = (Get-ToolVersion "ffmpeg" "9.0")
}

# Assemble Manifest
$manifest = [ordered]@{
    "schema" = 1
    "version" = $Version
    "git_sha" = $gitSha
    "dirty" = $dirty
    "built_at" = $builtAt
    "builder" = "windows-dev"
    "signed" = $isSigned
    "files" = [ordered]@{
        "l4setup.exe" = [ordered]@{
            "sha256" = $setupSha
            "size" = $setupSize
        }
    }
    "payload_sha256" = [ordered]@{
        "x86" = $payloadX86Sha
        "x64" = $payloadX64Sha
    }
    "components" = $components
    "min_os" = "6.1"
    "arch" = @("x86", "x64")
}

$manifestJsonPath = "$DistDir\l4tools-release.json"
$manifestJson = $manifest | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText($manifestJsonPath, $manifestJson, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "  [OK] Manifest created: $manifestJsonPath"

# SHA256 of manifest
$manifestSha = Get-FileSha256 $manifestJsonPath
Write-Host "  - l4tools-release.json: $manifestSha"

# Write SHA256SUMS in standard sha256sum format
$sha256SumsPath = "$DistDir\SHA256SUMS"
$sha256SumsContent = "$setupSha  l4setup.exe`n$manifestSha  l4tools-release.json`n"
[System.IO.File]::WriteAllText($sha256SumsPath, $sha256SumsContent, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "  [OK] SHA256SUMS created: $sha256SumsPath"

Write-Host "`nRelease packaging verification completed successfully!"
return $manifest
