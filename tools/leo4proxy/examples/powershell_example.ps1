<#
.SYNOPSIS
    Leo4 IoT PowerShell Event Publisher (extra_service role)
    Connects to local Mosquitto Bridge (127.0.0.1:1883) via plain TCP (No-SSL).
    Publishes telemetry events in a 10-minute cycle to topic 'dev/<SN>/evt' (QoS 1, Retain 0)
    with MQTT 5.0 User Properties and hardware event payload.

.PARAMETER IntervalSeconds
    Interval in seconds between event publications (default: 600 = 10 minutes).

.PARAMETER Once
    Send a single event and exit immediately.

.PARAMETER BrokerHost
    MQTT Broker IP or Hostname (default: 127.0.0.1).

.PARAMETER BrokerPort
    MQTT Broker Port (default: 1883).

.PARAMETER Username
    MQTT Username for ACL authentication (default: extra_service).

.PARAMETER CustomSn
    Manually override the Device Serial Number.
#>

[CmdletBinding()]
param(
    [string]$Role = "extra_service",
    [int]$IntervalSeconds = 600,
    [switch]$Once,
    [string]$BrokerHost = "127.0.0.1",
    [int]$BrokerPort = 1883,
    [string]$Username = "",
    [string]$CustomSn = ""
)

if ([string]::IsNullOrWhiteSpace($Username)) {
    $Username = $Role
}

$OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  Leo4 IoT PowerShell Event Publisher ($Role role)" -ForegroundColor Cyan
Write-Host "  Target: Mosquitto Bridge ($BrokerHost`:$BrokerPort, Plain TCP No-SSL)" -ForegroundColor Cyan
Write-Host "  Interval: $IntervalSeconds sec, Topic: dev/<SN>/evt (QoS 1, Retain 0)" -ForegroundColor Cyan
Write-Host "================================================================`n" -ForegroundColor Cyan

# 1. Resolve Device SN
$deviceSn = $CustomSn
if ([string]::IsNullOrWhiteSpace($deviceSn)) {
    try {
        $deviceSn = (Invoke-RestMethod -Uri "http://127.0.0.1:18443/_leo4/sn" -TimeoutSec 2).Trim()
        Write-Host "[INFO] Discovered active Device SN from local proxy: $deviceSn" -ForegroundColor Green
    } catch {
        $envSn = $env:DEVICE_SN
        if (-not [string]::IsNullOrWhiteSpace($envSn)) {
            $deviceSn = $envSn
        } else {
            $deviceSn = "a3b1234567c10221d290825"
        }
        Write-Host "[INFO] Using configured Device SN: $deviceSn" -ForegroundColor Yellow
    }
}

$topic = "dev/$deviceSn/evt"

# Presence configuration according to AGENTS.md
if ($Role -eq "main_app") {
    $presenceTopic = "dev/$deviceSn/app"
    $onlinePayload = "app_online"
    $offlinePayload = "app_offline"
    $clientSuffix = "main_ps"
} else {
    $presenceTopic = "dev/$deviceSn/svc"
    $onlinePayload = "svc_online"
    $offlinePayload = "svc_offline"
    $clientSuffix = "extra_ps"
}

# 2. Locate mosquitto_pub utility
$mosquittoPubPath = $null
$candidatePaths = @(
    "D:\Platerra26\tools\mosquitto\mosquitto_pub.exe",
    "C:\Program Files\mosquitto\mosquitto_pub.exe",
    "C:\mosquitto\mosquitto_pub.exe",
    (Get-Command "mosquitto_pub" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue)
)

foreach ($path in $candidatePaths) {
    if ($path -and (Test-Path $path)) {
        $mosquittoPubPath = $path
        break
    }
}

# Function to publish Presence status message (retain = true)
function Publish-MqttPresence {
    param(
        [string]$Status
    )

    Write-Host "[PRESENCE] Publishing status: $presenceTopic = $Status (retain=true)" -ForegroundColor Magenta
    if ($mosquittoPubPath) {
        $argsList = @(
            "-h", $BrokerHost,
            "-p", $BrokerPort.ToString(),
            "-V", "5",
            "-u", $Username,
            "-i", "$($deviceSn)_$($clientSuffix)_pres",
            "-t", $presenceTopic,
            "-m", $Status,
            "-r",
            "-q", "1"
        )
        $proc = Start-Process -FilePath $mosquittoPubPath -ArgumentList $argsList -NoNewWindow -PassThru -Wait
        return ($proc.ExitCode -eq 0)
    } else {
        $pyScript = "$PSScriptRoot\python_client.py"
        if (Test-Path $pyScript) {
            # Python fallback handles presence
            return $true
        }
    }
    return $false
}

# Function to publish via mosquitto_pub CLI or Fallback Python helper
function Publish-MqttEvent {
    param(
        [string]$PayloadJson,
        [int]$EventId,
        [string]$IsoTime,
        [string]$CorrId,
        [long]$UnixTs
    )

    if ($mosquittoPubPath) {
        # Using native mosquitto_pub CLI with LWT and MQTT 5.0 User Properties
        $argsList = @(
            "-h", $BrokerHost,
            "-p", $BrokerPort.ToString(),
            "-V", "5",
            "-u", $Username,
            "-i", "$($deviceSn)_$clientSuffix",
            "-t", $topic,
            "-q", "1",
            "-m", $PayloadJson,
            "--will-topic", $presenceTopic,
            "--will-payload", $offlinePayload,
            "--will-retain",
            "--will-qos", "1",
            "-D", "publish", "user-property", "event_type_code", "888",
            "-D", "publish", "user-property", "dev_event_id", $EventId.ToString(),
            "-D", "publish", "user-property", "dev_timestamp", $UnixTs.ToString(),
            "-D", "publish", "user-property", "correlation_id", $CorrId
        )

        $proc = Start-Process -FilePath $mosquittoPubPath -ArgumentList $argsList -NoNewWindow -PassThru -Wait
        if ($proc.ExitCode -eq 0) {
            Write-Host "    [ACK] Event delivered successfully via mosquitto_pub (ExitCode=0)!" -ForegroundColor Green
            return $true
        } else {
            Write-Host "    [WARN] mosquitto_pub exited with code: $($proc.ExitCode)" -ForegroundColor Red
            return $false
        }
    } else {
        # Fallback via Python helper
        Write-Host "    [INFO] mosquitto_pub.exe not found on disk. Invoking Python MQTT publisher..." -ForegroundColor Gray
        $pyScript = "$PSScriptRoot\python_client.py"
        if (Test-Path $pyScript) {
            & uv run $pyScript --once --role $Role --port $BrokerPort --host $BrokerHost
            return $true
        }
    }
    return $false
}

# 3. Main Publication Loop
$iteration = 0
$baseEventId = 36823

try {
    # Publish initial online presence
    $null = Publish-MqttPresence -Status $onlinePayload

    while ($true) {
        $iteration++
        $devEventId = $baseEventId + ($iteration - 1)
        $now = Get-Date
        $isoTime = $now.ToString("yyyy-MM-ddTHH:mm:sszzz")
        $unixTs = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
        $corrId = [Guid]::NewGuid().ToString()

        $payloadObj = [PSCustomObject]@{
            "101" = $devEventId
            "102" = $isoTime
            "200" = 888
            "300" = @(
                [PSCustomObject]@{
                    "301" = "044AFE42C76781"
                    "302" = 6
                    "303" = 0
                }
            )
        }

        $payloadJson = $payloadObj | ConvertTo-Json -Compress -Depth 5

        Write-Host "`n[$($now.ToString('yyyy-MM-dd HH:mm:ss'))] Publishing Event #$iteration`:" -ForegroundColor Cyan
        Write-Host "  Topic:           $topic"
        Write-Host "  QoS:             1 (Retain: 0)"
        Write-Host "  Payload:         $payloadJson"
        Write-Host "  User Properties: event_type_code=888, dev_event_id=$devEventId, dev_timestamp=$unixTs, correlation_id=$corrId"

        $null = Publish-MqttEvent -PayloadJson $payloadJson -EventId $devEventId -IsoTime $isoTime -CorrId $corrId -UnixTs $unixTs

        if ($Once) {
            Write-Host "`n[INFO] Single event sent (-Once). Exiting." -ForegroundColor Green
            break
        }

        Write-Host "`n[SLEEP] Waiting $IntervalSeconds seconds until next event publication..." -ForegroundColor DarkGray
        Start-Sleep -Seconds $IntervalSeconds
    }
} catch {
    Write-Host "`n[SHUTDOWN] Terminated by user." -ForegroundColor Yellow
} finally {
    # Publish offline status on normal exit
    $null = Publish-MqttPresence -Status $offlinePayload
    Write-Host "[SUCCESS] PowerShell Client ($Role) completed." -ForegroundColor Cyan
}
