[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PfxPath,
    [string]$Version,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$toolsRoot = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
if (-not $Version) { $Version = (Get-Content -LiteralPath "$toolsRoot\version.txt" -Raw).Trim() }
$stageRoot = (Resolve-Path -LiteralPath "$toolsRoot\dist\.stage").Path
$expectedStage = [IO.Path]::GetFullPath("$toolsRoot\dist\.stage")
if ($stageRoot -ne $expectedStage) { throw 'Unexpected staging directory.' }
$resourceDir = "$toolsRoot\l4setup\res"
$distDir = "$toolsRoot\dist"
$resolvedPfx = (Resolve-Path -LiteralPath $PfxPath).Path
$oldPfx = $env:L4TOOLS_SIGN_PFX
try {
    $env:L4TOOLS_SIGN_PFX = $resolvedPfx
    foreach ($arch in @('x86', 'x64')) {
        $stage = "$stageRoot\$arch"
        if (-not (Test-Path -LiteralPath $stage -PathType Container)) {
            throw "Missing staged payload: $stage"
        }
        & "$PSScriptRoot\Sign-Executables.ps1" -TargetPath $stage -TimestampUrl $TimestampUrl
        $payload = "$resourceDir\payload_$arch.bin"
        $stagePayload = "$stageRoot\payload_$arch.bin"
        if (Test-Path -LiteralPath $payload) { Remove-Item -LiteralPath $payload -Force }
        [System.IO.Compression.ZipFile]::CreateFromDirectory(
            $stage, $payload, [System.IO.Compression.CompressionLevel]::Optimal, $false)
        Copy-Item -LiteralPath $payload -Destination $stagePayload -Force
    }

    Push-Location "$toolsRoot\l4setup"
    try {
        & cmd.exe /c build.cmd
        if ($LASTEXITCODE -ne 0) { throw 'l4setup build failed.' }
    } finally { Pop-Location }

    $setupExe = "$toolsRoot\l4setup\bin\l4setup.exe"
    & "$PSScriptRoot\Sign-Executables.ps1" -TargetPath $setupExe -TimestampUrl $TimestampUrl
    Copy-Item -LiteralPath $setupExe -Destination "$distDir\l4setup.exe" -Force

    & "$PSScriptRoot\New-ReleaseManifest.ps1" -Version $Version `
        -ToolsRoot $toolsRoot -DistDir $distDir | Out-Null
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'Manifest generation failed.' }
    $manifest = Get-Content -LiteralPath "$distDir\l4tools-release.json" -Raw | ConvertFrom-Json
    if ($manifest.version -ne $Version) { throw 'Release version mismatch.' }
    $signature = Get-AuthenticodeSignature -LiteralPath "$distDir\l4setup.exe"
    if (-not $signature.SignerCertificate -or -not $signature.TimeStamperCertificate -or $signature.Status -ne 'Valid') {
        throw 'Final setup signature is missing or damaged.'
    }
    Write-Host "Signed release $Version prepared in $distDir"
} finally {
    $env:L4TOOLS_SIGN_PFX = $oldPfx
}
