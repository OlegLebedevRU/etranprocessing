[CmdletBinding()]
param(
    [string]$ToolsRoot,
    [string]$DistDir,
    [string]$StageDir,
    [string]$ResDir
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.IO.Compression.FileSystem

if (-not $ToolsRoot) {
    $ToolsRoot = (Resolve-Path "$PSScriptRoot\..").Path
} else {
    $ToolsRoot = (Resolve-Path $ToolsRoot).Path
}

$RepoRoot = (Resolve-Path "$ToolsRoot\..").Path

if (-not $DistDir) {
    $DistDir = "$ToolsRoot\dist"
}
if (-not $StageDir) {
    $StageDir = "$DistDir\.stage"
}
if (-not $ResDir) {
    $ResDir = "$ToolsRoot\l4setup\res"
}

if (-not (Test-Path $DistDir)) { New-Item -ItemType Directory -Path $DistDir -Force | Out-Null }
if (-not (Test-Path $StageDir)) { New-Item -ItemType Directory -Path $StageDir -Force | Out-Null }
if (-not (Test-Path $ResDir)) { New-Item -ItemType Directory -Path $ResDir -Force | Out-Null }

Write-Host "======================================================="
Write-Host " Staging Components and Packaging Release Payloads"
Write-Host " Tools Root: $ToolsRoot"
Write-Host " Stage Dir:  $StageDir"
Write-Host " Target Res: $ResDir"
Write-Host "======================================================="

$defaultL4supervJson = @'
{
  "base_path": "C:\\l4tools",
  "proxy_url": "http://127.0.0.1:18443/_leo4/info",
  "poll_interval_sec": 15,
  "watchdog_interval_sec": 10,
  "standby_poll_sec": 5,
  "pending_pin_check_sec": 30,
  "watchdog_enabled": true,
  "mosquitto_port": 1883,
  "auto_reset_on_clone": true,
  "services": {
    "leo4proxy": { "auto_start": true },
    "mosquitto": { "auto_start": true },
    "l4con":     { "auto_start": true },
    "l4desk":    { "auto_start": true, "mode": "user_session_high_il", "args": "--run --presence-interval 30" }
  }
}
'@

foreach ($arch in @("x86", "x64")) {
    $targetStage = "$StageDir\$arch"
    Write-Host "`nStaging architecture: $arch -> $targetStage"
    if (Test-Path $targetStage) {
        Remove-Item -Path $targetStage -Recurse -Force
    }
    New-Item -ItemType Directory -Path $targetStage -Force | Out-Null

    # Required subdirectories
    $subdirs = @("leo4proxy", "mosquitto\log", "l4con", "l4superv", "l4pin", "l4desk", "l4sql", "ffmpeg\log")
    foreach ($sub in $subdirs) {
        $p = "$targetStage\$sub"
        if (-not (Test-Path $p)) { New-Item -ItemType Directory -Path $p -Force | Out-Null }
    }

    # 1. leo4proxy
    $srcExe = "$ToolsRoot\leo4proxy\bin\$arch\leo4proxy.exe"
    if (-not (Test-Path $srcExe)) { throw "Binary not found: $srcExe" }
    Copy-Item $srcExe "$targetStage\leo4proxy\leo4proxy.exe" -Force
    Get-ChildItem -Path "$ToolsRoot\leo4proxy" -Filter "leo4proxy_*.cmd" | ForEach-Object {
        Copy-Item $_.FullName "$targetStage\leo4proxy\" -Force
    }
    if (Test-Path "$ToolsRoot\leo4proxy\README.md") {
        Copy-Item "$ToolsRoot\leo4proxy\README.md" "$targetStage\leo4proxy\" -Force
    }

    # 2. mosquitto (universal 32-bit for both x86 and x64)
    $srcMosq = "$ToolsRoot\mosquitto\mosquitto.exe"
    if (-not (Test-Path $srcMosq)) { throw "Binary not found: $srcMosq" }
    Copy-Item $srcMosq "$targetStage\mosquitto\mosquitto.exe" -Force
    if (Test-Path "$ToolsRoot\mosquitto\acl.conf") {
        Copy-Item "$ToolsRoot\mosquitto\acl.conf" "$targetStage\mosquitto\" -Force
    }
    if (Test-Path "$ToolsRoot\mosquitto\install_mosquitto.cmd") {
        Copy-Item "$ToolsRoot\mosquitto\install_mosquitto.cmd" "$targetStage\mosquitto\" -Force
    }
    if (Test-Path "$ToolsRoot\mosquitto\install_mosquitto.ps1") {
        Copy-Item "$ToolsRoot\mosquitto\install_mosquitto.ps1" "$targetStage\mosquitto\" -Force
    }
    if (Test-Path "$ToolsRoot\mosquitto\README.md") {
        Copy-Item "$ToolsRoot\mosquitto\README.md" "$targetStage\mosquitto\" -Force
    }
    # Clean any local state from mosquitto
    if (Test-Path "$targetStage\mosquitto\mosquitto.conf") {
        Remove-Item "$targetStage\mosquitto\mosquitto.conf" -Force
    }

    # 3. l4con
    $srcCon = "$ToolsRoot\l4con\bin\$arch\l4con.exe"
    if (-not (Test-Path $srcCon)) { throw "Binary not found: $srcCon" }
    Copy-Item $srcCon "$targetStage\l4con\l4con.exe" -Force
    Get-ChildItem -Path "$ToolsRoot\l4con" -Filter "l4con_*.cmd" | ForEach-Object {
        Copy-Item $_.FullName "$targetStage\l4con\" -Force
    }
    if (Test-Path "$ToolsRoot\l4con\README.md") { Copy-Item "$ToolsRoot\l4con\README.md" "$targetStage\l4con\" -Force }
    if (Test-Path "$ToolsRoot\l4con\CHANGELOG.md") { Copy-Item "$ToolsRoot\l4con\CHANGELOG.md" "$targetStage\l4con\" -Force }

    # 4. l4superv
    $srcSuperv = "$ToolsRoot\l4superv\bin\$arch\l4superv.exe"
    if (-not (Test-Path $srcSuperv)) { throw "Binary not found: $srcSuperv" }
    Copy-Item $srcSuperv "$targetStage\l4superv\l4superv.exe" -Force
    Get-ChildItem -Path "$ToolsRoot\l4superv" -Filter "l4superv_*.cmd" | ForEach-Object {
        Copy-Item $_.FullName "$targetStage\l4superv\" -Force
    }
    if (Test-Path "$ToolsRoot\l4superv\l4install_run.cmd") {
        Copy-Item "$ToolsRoot\l4superv\l4install_run.cmd" "$targetStage\l4superv\" -Force
    }
    if (Test-Path "$ToolsRoot\l4superv\README.md") { Copy-Item "$ToolsRoot\l4superv\README.md" "$targetStage\l4superv\" -Force }
    if (Test-Path "$ToolsRoot\l4superv\CHANGELOG.md") { Copy-Item "$ToolsRoot\l4superv\CHANGELOG.md" "$targetStage\l4superv\" -Force }

    # 5. l4pin
    $srcPin = "$ToolsRoot\l4pin\bin\$arch\l4pin.exe"
    if (-not (Test-Path $srcPin)) { throw "Binary not found: $srcPin" }
    Copy-Item $srcPin "$targetStage\l4pin\l4pin.exe" -Force
    if (Test-Path "$ToolsRoot\l4pin\install_cert.cmd") {
        Copy-Item "$ToolsRoot\l4pin\install_cert.cmd" "$targetStage\l4pin\" -Force
    }
    if (Test-Path "$ToolsRoot\l4pin\README.md") { Copy-Item "$ToolsRoot\l4pin\README.md" "$targetStage\l4pin\" -Force }
    if (Test-Path "$ToolsRoot\l4pin\CHANGELOG.md") { Copy-Item "$ToolsRoot\l4pin\CHANGELOG.md" "$targetStage\l4pin\" -Force }
    if (Test-Path "$ToolsRoot\l4pin\USER_GUIDE.md") { Copy-Item "$ToolsRoot\l4pin\USER_GUIDE.md" "$targetStage\l4pin\" -Force }

    # 6. l4desk
    $srcDesk = "$ToolsRoot\l4desk\bin\$arch\l4desk.exe"
    if (-not (Test-Path $srcDesk)) { throw "Binary not found: $srcDesk" }
    Copy-Item $srcDesk "$targetStage\l4desk\l4desk.exe" -Force
    Get-ChildItem -Path "$ToolsRoot\l4desk" -Filter "l4desk_*.cmd" | ForEach-Object {
        Copy-Item $_.FullName "$targetStage\l4desk\" -Force
    }
    if (Test-Path "$ToolsRoot\l4desk\README.md") { Copy-Item "$ToolsRoot\l4desk\README.md" "$targetStage\l4desk\" -Force }
    if (Test-Path "$ToolsRoot\l4desk\CHANGELOG.md") { Copy-Item "$ToolsRoot\l4desk\CHANGELOG.md" "$targetStage\l4desk\" -Force }

    # 7. l4sql
    $srcSql = "$ToolsRoot\l4sql\bin\$arch\l4sql.exe"
    if (-not (Test-Path $srcSql)) { throw "Binary not found: $srcSql" }
    Copy-Item $srcSql "$targetStage\l4sql\l4sql.exe" -Force
    if (Test-Path "$ToolsRoot\l4sql\README.md") { Copy-Item "$ToolsRoot\l4sql\README.md" "$targetStage\l4sql\" -Force }

    # 8. ffmpeg
    $srcFfmpeg = "$RepoRoot\ffmpeg-win32\$arch\ffmpeg.exe"
    if (-not (Test-Path $srcFfmpeg)) {
        if (Test-Path "$ToolsRoot\ffmpeg\$arch\ffmpeg.exe") {
            $srcFfmpeg = "$ToolsRoot\ffmpeg\$arch\ffmpeg.exe"
        } else {
            throw "FFmpeg binary not found for $arch at $srcFfmpeg"
        }
    }
    Copy-Item $srcFfmpeg "$targetStage\ffmpeg\ffmpeg.exe" -Force
    if (Test-Path "$ToolsRoot\ffmpeg\LICENSE") { Copy-Item "$ToolsRoot\ffmpeg\LICENSE" "$targetStage\ffmpeg\" -Force }
    if (Test-Path "$ToolsRoot\ffmpeg\VERSION.txt") { Copy-Item "$ToolsRoot\ffmpeg\VERSION.txt" "$targetStage\ffmpeg\" -Force }
    if (Test-Path "$ToolsRoot\ffmpeg\SOURCES.md") { Copy-Item "$ToolsRoot\ffmpeg\SOURCES.md" "$targetStage\ffmpeg\" -Force }
    if (Test-Path "$RepoRoot\ffmpeg-win32\$arch\ffmpeg.sha256") {
        Copy-Item "$RepoRoot\ffmpeg-win32\$arch\ffmpeg.sha256" "$targetStage\ffmpeg\" -Force
    }

    # Staging Root files
    if (Test-Path "$RepoRoot\docs\terminal-tools-user-guide.md") {
        Copy-Item "$RepoRoot\docs\terminal-tools-user-guide.md" "$targetStage\terminal-tools-user-guide.md" -Force
    } elseif (Test-Path "$ToolsRoot\dist_win7_sp1\terminal-tools-user-guide.md") {
        Copy-Item "$ToolsRoot\dist_win7_sp1\terminal-tools-user-guide.md" "$targetStage\terminal-tools-user-guide.md" -Force
    }

    if (Test-Path "$ToolsRoot\example_mosquitto.conf") {
        Copy-Item "$ToolsRoot\example_mosquitto.conf" "$targetStage\example_mosquitto.conf" -Force
    }

    [System.IO.File]::WriteAllText("$targetStage\l4superv.json", $defaultL4supervJson, (New-Object System.Text.UTF8Encoding($false)))

    Write-Host "  [OK] Staged files for $arch."

    # Pack into zip payload
    $outBin = "$ResDir\payload_$arch.bin"
    $stageCopyBin = "$StageDir\payload_$arch.bin"

    if (Test-Path $outBin) { Remove-Item $outBin -Force }
    if (Test-Path $stageCopyBin) { Remove-Item $stageCopyBin -Force }

    Write-Host "Compressing $arch payload into $outBin..."
    [System.IO.Compression.ZipFile]::CreateFromDirectory($targetStage, $outBin, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    Copy-Item $outBin $stageCopyBin -Force

    $binSize = (Get-Item $outBin).Length
    Write-Host ("  [OK] Created payload_{0}.bin: {1:N0} bytes ({2:N2} MB)" -f $arch, $binSize, ($binSize / 1MB))
}

# Generate payload.rc
$payloadRcPath = "$ResDir\payload.rc"
$payloadRcContent = @"
PAYLOAD_X86 RCDATA "payload_x86.bin"
PAYLOAD_X64 RCDATA "payload_x64.bin"
"@
[System.IO.File]::WriteAllText($payloadRcPath, $payloadRcContent, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "`n[OK] Generated payload.rc: $payloadRcPath"
Write-Host "Staging and payload packaging completed successfully."
