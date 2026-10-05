[CmdletBinding()]
param(
    [ValidateSet('Prepare', 'Sign')][string]$Mode = 'Prepare',
    [string]$PfxPath = $env:L4TOOLS_SIGN_PFX,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$toolsRoot = [IO.Path]::GetFullPath("$PSScriptRoot\..")
$version = '1.13.1'
$stageRoot = "$toolsRoot\dist\.stage"
$sealPath = "$toolsRoot\dist\fm-signing-input.json"

function Get-StageHashes {
    $entries = @()
    foreach ($arch in @('x86', 'x64')) {
        $root = "$stageRoot\$arch"
        foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File) {
            if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse staging entry.' }
            $entries += [ordered]@{
                arch = $arch
                path = $file.FullName.Substring($root.Length + 1)
                sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
            }
        }
    }
    return $entries
}

if ($Mode -eq 'Sign') {
    if (-not $PfxPath) { throw 'Specify -PfxPath; password comes only from L4TOOLS_SIGN_PFX_PASSWORD.' }
    $resolvedPfx = (Resolve-Path -LiteralPath $PfxPath).Path
    $repoRoot = [IO.Path]::GetFullPath("$toolsRoot\..") + '\'
    if ($resolvedPfx.StartsWith($repoRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Keep the PFX outside the repository.' }
    $seal = Get-Content -LiteralPath $sealPath -Raw | ConvertFrom-Json
    if ($seal.version -ne $version) { throw 'Signing input version mismatch.' }
    $current = @(Get-StageHashes)
    if ($current.Count -ne $seal.files.Count) { throw 'Staging inventory changed; prepare again.' }
    foreach ($entry in $seal.files) {
        $actual = @($current | Where-Object { $_.arch -eq $entry.arch -and $_.path -eq $entry.path })
        if ($actual.Count -ne 1 -or $actual[0].sha256 -ne $entry.sha256) { throw "Changed signing input: $($entry.arch)/$($entry.path)" }
    }
    if ((Get-Content -LiteralPath "$toolsRoot\version.txt" -Raw).Trim() -ne $version) { throw 'Suite version changed.' }
    foreach ($resource in $seal.resources) {
        if ((Get-FileHash -LiteralPath "$toolsRoot\$($resource.path)" -Algorithm SHA256).Hash -ne $resource.sha256) { throw 'Setup resource changed.' }
    }
    & "$PSScriptRoot\Complete-SignedRelease.ps1" -PfxPath $PfxPath -Version $version -SignOnly @('l4con') -TimestampUrl $TimestampUrl
    foreach ($entry in $seal.files) {
        if ($entry.path -in @('l4con\l4con.exe', 'l4superv\package-components.json')) { continue }
        $path = "$stageRoot\$($entry.arch)\$($entry.path)"
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Unchanged component modified: $path" }
    }
    foreach ($arch in @('x86', 'x64')) {
        foreach ($tool in @('l4con')) {
            Copy-Item -LiteralPath "$stageRoot\$arch\$tool\$tool.exe" -Destination "$toolsRoot\$tool\bin\$arch\$tool.exe" -Force
        }
    }
    foreach ($tool in @('l4con')) {
        Copy-Item -LiteralPath "$toolsRoot\$tool\bin\x86\$tool.exe" -Destination "$toolsRoot\$tool\bin\$tool.exe" -Force
    }
    Copy-Item -LiteralPath "$toolsRoot\dist\l4setup.exe" -Destination "$toolsRoot\l4setup\bin\x86\l4setup.exe" -Force
    Write-Host 'FM release signed and verified. Publication is a separate step; do not rebuild components.'
    return
}

if (Test-Path -LiteralPath $sealPath) {
    $oldSeal = Get-Content -LiteralPath $sealPath -Raw | ConvertFrom-Json
    if ($oldSeal.version -ne '1.13.0') { throw 'An FM signing packet already exists; preserve it and review before preparing again.' }
}
$previous = Get-Content -LiteralPath "$toolsRoot\dist\l4tools-release.json" -Raw | ConvertFrom-Json
if ($previous.version -ne '1.13.0' -or -not $previous.signed) { throw 'Expected signed 1.13.0 baseline.' }
$baselineSetup = "$toolsRoot\dist\l4setup.exe"
$baselineSignature = Get-AuthenticodeSignature -LiteralPath $baselineSetup
if ($baselineSignature.Status -ne 'Valid' -or -not $baselineSignature.TimeStamperCertificate -or
    (Get-FileHash -LiteralPath $baselineSetup -Algorithm SHA256).Hash.ToLowerInvariant() -ne $previous.files.'l4setup.exe'.sha256.ToLowerInvariant()) {
    throw 'Signed baseline installer mismatch.'
}
foreach ($arch in @('x86', 'x64')) {
    foreach ($entry in $previous.component_artifacts.$arch) {
        $path = "$stageRoot\$arch\$($entry.path)"
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256.ToLowerInvariant()) { throw "Baseline payload mismatch: $path" }
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($signature.Status -ne 'Valid' -or -not $signature.TimeStamperCertificate) { throw "Invalid baseline signature: $path" }
    }
}
$backup = "$toolsRoot\dist\.runtime-backup\fm-$version-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $stageRoot -Destination "$backup\stage" -Recurse
foreach ($name in @('l4setup.exe', 'l4tools-release.json', 'SHA256SUMS')) {
    Copy-Item -LiteralPath "$toolsRoot\dist\$name" -Destination $backup
}
if (Test-Path -LiteralPath $sealPath) {
    Copy-Item -LiteralPath $sealPath -Destination "$backup\fm-signing-input.json"
    Remove-Item -LiteralPath $sealPath
}
if ((Get-Content -LiteralPath "$toolsRoot\version.txt" -Raw).Trim() -ne $version) { throw 'Expected suite version 1.13.1.' }
$srcVersion = @'
#pragma once
#define L4SETUP_VERSION_MAJOR 1
#define L4SETUP_VERSION_MINOR 13
#define L4SETUP_VERSION_PATCH 1
#define L4SETUP_VERSION_BUILD 0
#define L4SETUP_VERSION_STRING "1.13.1"
#define L4SETUP_VERSION_WSTRING L"1.13.1"
'@
$resVersion = $srcVersion + @'

#define L4TOOLS_VERSION "1.13.1"
#define L4TOOLS_VERSION_RC 1,13,1,0
#define VER_FILEVERSION 1,13,1,0
#define VER_PRODUCTVERSION 1,13,1,0
#define VER_FILEVERSION_STR "1.13.1.0\0"
#define VER_PRODUCTVERSION_STR "1.13.1\0"
'@
$srcVersion | Set-Content -LiteralPath "$toolsRoot\l4setup\src\version.h" -Encoding ASCII
$resVersion | Set-Content -LiteralPath "$toolsRoot\l4setup\res\version.h" -Encoding ASCII
foreach ($tool in @('l4con')) {
    Push-Location "$toolsRoot\$tool"
    try {
        & cmd.exe /c build.cmd all
        if ($LASTEXITCODE -ne 0) { throw "$tool build/tests failed." }
    } finally { Pop-Location }
}
foreach ($arch in @('x86', 'x64')) {
    $stage = "$stageRoot\$arch"
    $binary = "$toolsRoot\l4con\bin\$arch\l4con.exe"
    if ((Get-Item -LiteralPath $binary).VersionInfo.ProductVersion -ne '1.12.1') { throw 'Unexpected FM agent version.' }
    Copy-Item -LiteralPath $binary -Destination "$stage\l4con\l4con.exe" -Force
    # Terminal-specific config/ACL comes only from the terminal during upgrade.
    foreach ($name in @('acl.conf','mosquitto.conf','mosquitto.conf.bak')) {
        $fixture = "$stage\mosquitto\$name"
        if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture }
    }
    & "$PSScriptRoot\New-PayloadInventory.ps1" -Stage $stage -Arch $arch -Version $version
    $payload = "$toolsRoot\l4setup\res\payload_$arch.bin"
    if (Test-Path -LiteralPath $payload) { Remove-Item -LiteralPath $payload -Force }
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $payload, [IO.Compression.CompressionLevel]::Optimal, $false)
    Copy-Item -LiteralPath $payload -Destination "$stageRoot\payload_$arch.bin" -Force
}
Push-Location "$toolsRoot\l4setup"
try {
    & cmd.exe /c run_tests.cmd
    if ($LASTEXITCODE -ne 0) { throw 'l4setup x86 tests failed.' }
    & cmd.exe /c 'run_tests.cmd x64'
    if ($LASTEXITCODE -ne 0) { throw 'l4setup x64 tests failed.' }
    & cmd.exe /c build.cmd all
    if ($LASTEXITCODE -ne 0) { throw 'l4setup build/tests failed.' }
} finally { Pop-Location }
Copy-Item -LiteralPath "$toolsRoot\l4setup\bin\l4setup.exe" -Destination "$toolsRoot\dist\l4setup.exe" -Force
& "$PSScriptRoot\New-ReleaseManifest.ps1" -Version $version -ToolsRoot $toolsRoot -DistDir "$toolsRoot\dist" -SkipVerifications | Out-Null
& "$PSScriptRoot\Test-PayloadIntegrity.ps1" -ToolsRoot $toolsRoot -SkipCaptureComparison
$resources = @('l4setup\src\version.h', 'l4setup\res\version.h', 'l4setup\res\network-profile.bin') | ForEach-Object {
    [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath "$toolsRoot\$_" -Algorithm SHA256).Hash }
}
[ordered]@{ version = $version; backup = $backup; files = @(Get-StageHashes); resources = @($resources) } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $sealPath -Encoding UTF8
Write-Host "Unsigned FM $version prepared. Run this script with -Mode Sign -PfxPath '<external PFX path>'."
