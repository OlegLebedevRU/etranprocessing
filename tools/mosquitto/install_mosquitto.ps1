# ==============================================================================
# Mosquitto Broker Installer & Configurator for Platerra
# ==============================================================================
param(
    [string]$TargetDir = $PSScriptRoot,
    [string]$CustomSn = $null
)

$ErrorActionPreference = "Stop"

if (-not $TargetDir) {
    $TargetDir = Split-Path -Parent $MyInvocation.MyCommand.Path
}
$TargetDir = [System.IO.Path]::GetFullPath($TargetDir).TrimEnd('\')
$mosquittoExe = Join-Path $TargetDir "mosquitto.exe"

# ------------------------------------------------------------------------------
# 0. Check Administrator Privileges & Auto-Elevate via UAC
# ------------------------------------------------------------------------------
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "=======================================================" -ForegroundColor Cyan
    Write-Host " Mosquitto Installation & Configuration" -ForegroundColor Cyan
    Write-Host " Target Directory: $TargetDir" -ForegroundColor Cyan
    Write-Host "=======================================================" -ForegroundColor Cyan
    Write-Host "[INFO] Administrator privileges required for environment and service configuration." -ForegroundColor Yellow
    Write-Host "[INFO] Requesting UAC elevation..." -ForegroundColor Yellow

    $scriptPath = $PSCommandPath
    if (-not $scriptPath) {
        $scriptPath = $MyInvocation.MyCommand.Definition
    }

    $argList = @(
        "-NoLogo",
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$scriptPath`"",
        "-TargetDir", "`"$TargetDir`""
    )
    if (-not [string]::IsNullOrWhiteSpace($CustomSn)) {
        $argList += @("-CustomSn", "`"$CustomSn`"")
    }

    try {
        $elevatedProc = Start-Process -FilePath "powershell.exe" -ArgumentList $argList -Verb RunAs -Wait -PassThru
        if ($elevatedProc.ExitCode -eq 0) {
            Write-Host "  -> Elevated installer completed successfully." -ForegroundColor Green
        } else {
            Write-Host "  -> [ERROR] Elevated installer finished with exit code $($elevatedProc.ExitCode)." -ForegroundColor Red
        }
        exit $elevatedProc.ExitCode
    } catch {
        Write-Host "  -> [ERROR] Failed to obtain UAC elevation: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "  -> Please right-click the script/cmd and choose 'Run as administrator'." -ForegroundColor Red
        exit 1
    }
}

if (-not (Test-Path $mosquittoExe)) {
    Write-Error "mosquitto.exe not found in $TargetDir"
    exit 1
}

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host " Mosquitto Installation & Configuration" -ForegroundColor Cyan
Write-Host " Target Directory: $TargetDir" -ForegroundColor Cyan
Write-Host "=======================================================" -ForegroundColor Cyan

# ------------------------------------------------------------------------------
# 1. Retrieve Device SN from leo4proxy
# ------------------------------------------------------------------------------
$deviceSn = $null

if (-not [string]::IsNullOrWhiteSpace($CustomSn)) {
    $deviceSn = $CustomSn.Trim()
    Write-Host "[1/4] Using specified SN: $deviceSn" -ForegroundColor Yellow
} else {
    Write-Host "[1/4] Querying Device SN from leo4proxy..." -ForegroundColor Yellow

    # Search candidates for leo4proxy.exe
    $leo4Candidates = @(
        (Join-Path $TargetDir "..\leo4proxy\leo4proxy.exe"),
        (Join-Path $TargetDir "leo4proxy.exe"),
        "D:\Platerra26\tools\leo4proxy\leo4proxy.exe",
        "D:\repo\platerra\Public\etranprocessing\tools\leo4proxy\bin\leo4proxy.exe",
        "C:\Platerra\leo4proxy.exe"
    )

    foreach ($cand in $leo4Candidates) {
        if (Test-Path $cand) {
            try {
                $output = & $cand --get-sn 2>$null
                if ($LASTEXITCODE -eq 0 -and $output) {
                    $snVal = ($output | Out-String).Trim()
                    if ($snVal.Length -ge 4 -and -not ($snVal -match "Error")) {
                        $deviceSn = $snVal
                        Write-Host "  -> Obtained SN from CLI ($cand): $deviceSn" -ForegroundColor Green
                        break
                    }
                }
            } catch {
                # continue to next candidate or HTTP fallback
            }
        }
    }

    # Fallback to local HTTP REST endpoint if CLI was not available
    if (-not $deviceSn) {
        try {
            $resp = Invoke-WebRequest -Uri "http://127.0.0.1:18443/_leo4/sn" -TimeoutSec 2 -UseBasicParsing -ErrorAction SilentlyContinue
            if ($resp.StatusCode -eq 200 -and $resp.Content) {
                $snVal = $resp.Content.Trim()
                if ($snVal.Length -ge 4 -and -not ($snVal -match "Error")) {
                    $deviceSn = $snVal
                    Write-Host "  -> Obtained SN from HTTP endpoint: $deviceSn" -ForegroundColor Green
                }
            }
        } catch {
            # HTTP endpoint unavailable
        }
    }
}

# ------------------------------------------------------------------------------
# 2. Generate mosquitto.conf and acl.conf (UTF-8 No-BOM)
# ------------------------------------------------------------------------------
Write-Host "[2/4] Generating configuration files..." -ForegroundColor Yellow

$logDir = Join-Path $TargetDir "log"
if (-not (Test-Path $logDir)) {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
}

$logPathForward = ($logDir -replace '\\', '/') + "/mosquitto.log"
$aclPathForward = ($TargetDir -replace '\\', '/') + "/acl.conf"

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

if ($deviceSn) {
    Write-Host "  -> Generating Bridge configuration for SN: $deviceSn" -ForegroundColor Green

    $confContent = @"
# ==============================================================================
# Mosquitto MQTT Broker Configuration
# Generated automatically for Device SN: $deviceSn
# ==============================================================================

# Local listener for internal terminal processes
listener 1883 127.0.0.1
allow_anonymous true

# Bridge configuration to leo4proxy (Native mTLS tunnel)
connection platerra-upstream
bridge_protocol_version mqttv50
address 127.0.0.1:18883

# Remote client identifier for external broker
remote_clientid $deviceSn

# Disable Mosquitto `$SYS status topics (required for external broker compatibility)
try_private false
notifications false

# Topic routing rules (topic <pattern> <direction> <QoS>)
# 1) Outbound responses and events from terminal to server:
topic dev/$deviceSn/out out 0
topic dev/$deviceSn/# out 1

# 2) Inbound commands from server to terminal:
topic srv/$deviceSn/rsp in 1
topic srv/$deviceSn/# in 1

# Connection reliability and keep-alive
cleansession true
restart_timeout 5 60
keepalive_interval 60

# Persistence and logging
persistence false
log_dest file $logPathForward
log_type error
log_type warning
log_type notice
log_type information

# Optional ACL topic isolation (uncomment if ACL enforcement is required):
# acl_file $aclPathForward
"@

    $aclContent = @"
# ==============================================================================
# Mosquitto ACL Configuration for Device SN: $deviceSn
# ==============================================================================

# Permissions for main UI / master application
user main_app
topic readwrite srv/$deviceSn/#
topic readwrite dev/$deviceSn/#

# Permissions for auxiliary service (publish events only)
user extra_service
topic write dev/$deviceSn/evt
topic write dev/$deviceSn/svc
"@

    $aclFile = Join-Path $TargetDir "acl.conf"
    [System.IO.File]::WriteAllText($aclFile, $aclContent, $utf8NoBom)
    Write-Host "  -> Created: $aclFile" -ForegroundColor Cyan

} else {
    Write-Host "  -> [WARNING] Device SN not detected! Generating Neutral configuration (No Bridge)." -ForegroundColor Red

    $confContent = @"
# ==============================================================================
# Mosquitto MQTT Broker Configuration (Neutral Mode - Local Only, No Bridge)
# Device SN was not detected. External bridge is disabled.
# ==============================================================================

# Local listener for internal terminal processes
listener 1883 127.0.0.1
allow_anonymous true

# Persistence and logging
persistence false
log_dest file $logPathForward
log_type error
log_type warning
log_type notice
log_type information
"@
}

$confFile = Join-Path $TargetDir "mosquitto.conf"
[System.IO.File]::WriteAllText($confFile, $confContent, $utf8NoBom)
Write-Host "  -> Created: $confFile" -ForegroundColor Cyan

# Validate configuration syntax
try {
    $testResult = & $mosquittoExe -c $confFile --test-config 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  -> Configuration syntax validation: PASSED" -ForegroundColor Green
    } else {
        Write-Host "  -> [WARNING] Configuration syntax validation returned exit code $LASTEXITCODE : $testResult" -ForegroundColor Yellow
    }
} catch {
    # Ignore validation call errors
}

# ------------------------------------------------------------------------------
# 3. Configure Environment Variable MOSQUITTO_DIR
# ------------------------------------------------------------------------------
Write-Host "[3/4] Setting system environment variable MOSQUITTO_DIR..." -ForegroundColor Yellow
$envSetSuccess = $false
try {
    [System.Environment]::SetEnvironmentVariable("MOSQUITTO_DIR", $TargetDir, [System.EnvironmentVariableTarget]::Machine)
    $env:MOSQUITTO_DIR = $TargetDir
    Write-Host "  -> MOSQUITTO_DIR (Machine) successfully set to: $TargetDir" -ForegroundColor Green
    $envSetSuccess = $true
} catch {
    Write-Host "  -> [WARNING] Failed to set Machine environment variable: $($_.Exception.Message)" -ForegroundColor Red
    try {
        [System.Environment]::SetEnvironmentVariable("MOSQUITTO_DIR", $TargetDir, [System.EnvironmentVariableTarget]::User)
        $env:MOSQUITTO_DIR = $TargetDir
        Write-Host "  -> MOSQUITTO_DIR set to User scope: $TargetDir" -ForegroundColor Yellow
        $envSetSuccess = $true
    } catch {
        Write-Host "  -> [ERROR] Failed to set User environment variable: $($_.Exception.Message)" -ForegroundColor Red
    }
}

# ------------------------------------------------------------------------------
# 4. Service Re-installation & Restart
# ------------------------------------------------------------------------------
Write-Host "[4/4] Installing and restarting Mosquitto Windows Service..." -ForegroundColor Yellow

$proc = Get-Process -Name "mosquitto" -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "  -> Stopping mosquitto service / process..." -ForegroundColor Gray
    sc.exe stop mosquitto 2>$null | Out-Null
    $waitSec = 5
    while ($waitSec -gt 0 -and (Get-Process -Name "mosquitto" -ErrorAction SilentlyContinue)) {
        Start-Sleep -Milliseconds 500
        $waitSec -= 0.5
    }
    if (Get-Process -Name "mosquitto" -ErrorAction SilentlyContinue) {
        Stop-Process -Name "mosquitto" -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }
}

$serviceObj = Get-Service -Name "mosquitto" -ErrorAction SilentlyContinue
if ($serviceObj) {
    Write-Host "  -> Removing existing service registration..." -ForegroundColor Gray
    sc.exe delete mosquitto 2>$null | Out-Null
    Start-Sleep -Seconds 1
}

# Install service pointing to current target directory
Push-Location $TargetDir
try {
    Write-Host "  -> Registering service via mosquitto.exe install..." -ForegroundColor Gray
    & $mosquittoExe install | Out-Null
    Start-Sleep -Seconds 1

    # Configure auto-restart on failure (SCM watchdog)
    sc.exe failure mosquitto reset= 86400 actions= restart/5000/restart/10000/restart/30000 2>$null | Out-Null

    Write-Host "  -> Starting service via sc.exe start mosquitto..." -ForegroundColor Gray
    sc.exe start mosquitto | Out-Null

    # Wait for service to enter Running state
    $waitSec = 6
    $finalSvc = $null
    while ($waitSec -gt 0) {
        $finalSvc = Get-CimInstance Win32_Service -Filter "Name='mosquitto'" -ErrorAction SilentlyContinue
        if ($finalSvc -and $finalSvc.State -eq 'Running') {
            break
        }
        Start-Sleep -Milliseconds 500
        $waitSec -= 0.5
    }
    if ($finalSvc -and $finalSvc.State -eq 'Running') {
        Write-Host "=======================================================" -ForegroundColor Green
        Write-Host " Mosquitto Service Status:" -ForegroundColor Green
        Write-Host " Name:        $($finalSvc.Name)" -ForegroundColor Green
        Write-Host " DisplayName: $($finalSvc.DisplayName)" -ForegroundColor Green
        Write-Host " State:       $($finalSvc.State)" -ForegroundColor Green
        Write-Host " StartMode:   $($finalSvc.StartMode)" -ForegroundColor Green
        Write-Host " PathName:    $($finalSvc.PathName)" -ForegroundColor Green
        Write-Host " Config Dir:  $TargetDir" -ForegroundColor Green
        Write-Host "=======================================================" -ForegroundColor Green
    } else {
        Write-Host "  -> [WARNING] Service 'mosquitto' is not in Running state." -ForegroundColor Yellow
        if ($finalSvc) {
            Write-Host "     Current State: $($finalSvc.State), ExitCode: $($finalSvc.ExitCode)" -ForegroundColor Yellow
        }
    }
} finally {
    Pop-Location
}
