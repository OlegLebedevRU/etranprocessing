# Leo4Proxy PowerShell Client Example
# Demonstrates querying device metadata and calling backend API via local Leo4Proxy

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  Leo4 IoT PowerShell Client via Leo4Proxy (SChannel mTLS)" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan

# 1. Query Device Info & SN from Local Proxy
Write-Host "`n[1] Querying Device Metadata from http://127.0.0.1:18443/_leo4/info..." -ForegroundColor Yellow
try {
    $info = Invoke-RestMethod -Uri "http://127.0.0.1:18443/_leo4/info" -TimeoutSec 3
    Write-Host "  Device SN:       $($info.sn)" -ForegroundColor Green
    Write-Host "  Subject Email:   $($info.email)"
    Write-Host "  Issuer:          $($info.issuer)"
    Write-Host "  Serial Number:   $($info.serial)"
    Write-Host "  SHA-1 Thumb:     $($info.thumbprint)"
    Write-Host "  Valid Until:     $($info.not_after)"
    Write-Host "  MQTT Local Port: $($info.endpoints.mqtt_local)"
    Write-Host "  HTTP Local Port: $($info.endpoints.http_local)"
} catch {
    Write-Error "Failed to query local proxy. Please ensure leo4proxy.exe is running! Error: $_"
    return
}

# 2. Query plain Device SN
$deviceSn = Invoke-RestMethod -Uri "http://127.0.0.1:18443/_leo4/sn"
Write-Host "`n[2] Plain SN query (http://127.0.0.1:18443/_leo4/sn): $deviceSn" -ForegroundColor Yellow

# 3. Call backend API with mutual TLS (handled transparently by proxy)
Write-Host "`n[3] Calling LicenseBilling API via http://127.0.0.1:18443/licensebilling/..." -ForegroundColor Yellow
try {
    $body = "function=check&Signature=TUI9MSBCQkhFNTMyMUI0MDAxNDkwfENQVT0xIEJGRUJGQkZGMDAwODA2QzE="
    $resp = Invoke-WebRequest -Uri "http://127.0.0.1:18443/licensebilling/" `
                              -Method Post `
                              -Body $body `
                              -ContentType "application/x-www-form-urlencoded" `
                              -TimeoutSec 5

    Write-Host "  HTTP Status:     $([int]$resp.StatusCode)" -ForegroundColor Green
    Write-Host "  Response XML:    $($resp.Content.Trim())" -ForegroundColor Green
} catch {
    Write-Error "Backend request failed: $_"
}

Write-Host "`n[SUCCESS] PowerShell verification completed." -ForegroundColor Cyan
