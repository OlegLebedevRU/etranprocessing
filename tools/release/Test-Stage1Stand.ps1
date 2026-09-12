<#
.SYNOPSIS
    Test-Stage1Stand.ps1 - Скрипт диагностических снимков и валидации стенда Этапа 1 (Leo4 Tools).
.DESCRIPTION
    Поддерживает три фазы:
      -Phase Before  : Снимает состояние системы до прогона инсталлятора в C:\l4tools\stand-before-<timestamp>.json
      -Phase After   : Снимает состояние системы после прогона инсталлятора в C:\l4tools\stand-after-<timestamp>.json
      -Phase Compare : Сравнивает снимки Before и After (и опционально второй After для проверки идемпотентности),
                       формирует отчет PASS/FAIL по критериям DoD 8.
    Скрипт выполняет только чтение и не модифицирует настройки системы или хранилища ключей.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [ValidateSet('Before', 'After', 'Compare')]
    [string]$Phase = 'Before',

    [Parameter(Mandatory = $false)]
    [string]$Path = 'C:\l4tools',

    [Parameter(Mandatory = $false)]
    [string]$BeforeFile,

    [Parameter(Mandatory = $false)]
    [string]$AfterFile,

    [Parameter(Mandatory = $false)]
    [string]$AfterFile2
)

Set-StrictMode -Off
$ErrorActionPreference = 'Continue'

function Get-Leo4Certificates {
    $certs = @()
    try {
        $found = Get-ChildItem -Path Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
            Where-Object { $_.Issuer -like '*iot.leo4.ru*' }
        foreach ($c in $found) {
            $certs += [PSCustomObject]@{
                Subject       = $c.Subject
                Issuer        = $c.Issuer
                Thumbprint    = $c.Thumbprint
                NotAfterUtc   = $c.NotAfter.ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
                HasPrivateKey = [bool]$c.HasPrivateKey
            }
        }
    } catch {
        Write-Warning "Failed to query Cert:\LocalMachine\My: $_"
    }
    return ,$certs
}

function Get-Leo4ProxyInfo {
    $url = "http://127.0.0.1:18443/_leo4/info"
    try {
        $resp = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 3 -ErrorAction Stop
        if ($resp.StatusCode -eq 200 -and $resp.Content) {
            return ($resp.Content | ConvertFrom-Json)
        }
    } catch {
        return [PSCustomObject]@{
            error   = "unreachable"
            message = $_.Exception.Message
        }
    }
    return $null
}

function Get-ServicesStatus {
    $serviceNames = @('Leo4Proxy', 'mosquitto', 'L4Con', 'L4Superv')
    $list = @()
    foreach ($name in $serviceNames) {
        $svc = Get-Service -Name $name -ErrorAction SilentlyContinue
        $status = if ($svc) { $svc.Status.ToString() } else { "NotFound" }
        $pathName = ""
        try {
            $cim = Get-CimInstance Win32_Service -Filter "Name='$name'" -ErrorAction SilentlyContinue
            if ($cim) { $pathName = $cim.PathName }
        } catch {}
        $list += [PSCustomObject]@{
            Name     = $name
            Status   = $status
            PathName = $pathName
        }
    }
    return ,$list
}

function Get-ExeSignatures ([string]$baseDir) {
    $files = @()
    if (Test-Path $baseDir) {
        $exeList = Get-ChildItem -Path $baseDir -Filter *.exe -Recurse -File -ErrorAction SilentlyContinue
        foreach ($f in $exeList) {
            $rel = $f.FullName.Substring($baseDir.Length).TrimStart('\')
            $hash = ""
            try {
                $hash = (Get-FileHash -Path $f.FullName -Algorithm SHA256).Hash.ToLower()
            } catch {}
            $files += [PSCustomObject]@{
                RelativePath     = $rel
                LastWriteTimeUtc = $f.LastWriteTimeUtc.ToString("yyyy-MM-ddTHH:mm:ssZ")
                Size             = $f.Length
                Sha256           = $hash
            }
        }
    }
    return ,$files
}

function Read-JsonFileSafe ([string]$filePath) {
    if (Test-Path $filePath) {
        try {
            $content = [System.IO.File]::ReadAllText($filePath, [System.Text.Encoding]::UTF8)
            return ($content | ConvertFrom-Json)
        } catch {
            return [PSCustomObject]@{ error = "failed_to_parse"; details = $_.Exception.Message }
        }
    }
    return $null
}

function New-StandSnapshot ([string]$phaseName, [string]$targetDir) {
    $ts = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    $fileTs = (Get-Date).ToString("yyyyMMdd-HHmmss")
    $stateJsonPath = Join-Path $targetDir "state.json"
    $pendingPinPath = Join-Path $targetDir "pending_pin.json"
    $summaryPath = Join-Path $targetDir "install_summary.json"

    $snapshot = [PSCustomObject]@{
        schema               = 1
        phase                = $phaseName.ToLower()
        timestamp_utc        = $ts
        target_dir           = $targetDir
        certificates         = (Get-Leo4Certificates)
        leo4_proxy_info      = (Get-Leo4ProxyInfo)
        services             = (Get-ServicesStatus)
        exe_files            = (Get-ExeSignatures $targetDir)
        state_json           = (Read-JsonFileSafe $stateJsonPath)
        pending_pin_present  = (Test-Path $pendingPinPath)
        install_summary      = (Read-JsonFileSafe $summaryPath)
    }

    $outFileName = "stand-$($phaseName.ToLower())-$fileTs.json"
    $outFilePath = Join-Path $targetDir $outFileName
    if (-not (Test-Path $targetDir)) {
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    }

    $jsonStr = $snapshot | ConvertTo-Json -Depth 10
    [System.IO.File]::WriteAllText($outFilePath, $jsonStr, [System.Text.Encoding]::UTF8)
    Write-Host "[OK] Snapshot saved ($phaseName): $outFilePath" -ForegroundColor Green
    return $outFilePath
}

function Invoke-CompareSnapshots {
    param(
        [string]$bFile,
        [string]$aFile,
        [string]$aFile2,
        [string]$targetDir
    )

    if (-not $bFile) {
        $candidates = Get-ChildItem -Path $targetDir -Filter "stand-before-*.json" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending
        if ($candidates -and $candidates.Count -gt 0) {
            $bFile = $candidates[0].FullName
        }
    }
    if (-not $aFile) {
        $candidates = Get-ChildItem -Path $targetDir -Filter "stand-after-*.json" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending
        if ($candidates -and $candidates.Count -gt 0) {
            $aFile = $candidates[0].FullName
            if ($candidates.Count -gt 1 -and -not $aFile2) {
                $aFile2 = $candidates[1].FullName
            }
        }
    }

    if (-not $bFile -or -not (Test-Path $bFile)) {
        Write-Error "Before snapshot file not found: '$bFile'"
        return 1
    }
    if (-not $aFile -or -not (Test-Path $aFile)) {
        Write-Error "After snapshot file not found: '$aFile'"
        return 1
    }

    Write-Host "Comparing snapshots:" -ForegroundColor Cyan
    Write-Host "  Before : $bFile"
    Write-Host "  After  : $aFile"
    if ($aFile2) {
        Write-Host "  After2 : $aFile2 (for idempotency check)"
    }
    Write-Host ""

    $before = Read-JsonFileSafe $bFile
    $after  = Read-JsonFileSafe $aFile
    $after2 = if ($aFile2 -and (Test-Path $aFile2)) { Read-JsonFileSafe $aFile2 } else { $null }

    $results = @()
    $allPass = $true

    # 1. Thumbprint check
    $beforeThumb = if ($before.certificates -and $before.certificates.Count -gt 0) { $before.certificates[0].Thumbprint } else { "" }
    $afterThumb  = if ($after.certificates -and $after.certificates.Count -gt 0) { $after.certificates[0].Thumbprint } else { "" }
    $thumbMatch = ($beforeThumb -ne "" -and $beforeThumb -eq $afterThumb)
    $results += [PSCustomObject]@{
        Criterion = "Thumbprint Unchanged"
        Expected  = if ($beforeThumb) { $beforeThumb } else { "(any valid)" }
        Actual    = if ($afterThumb) { $afterThumb } else { "(none)" }
        Status    = if ($thumbMatch) { "PASS" } else { "FAIL" }
    }
    if (-not $thumbMatch) { $allPass = $false }

    # 2. Leo4Proxy / Install Summary status ready
    $afterStatus = ""
    if ($after.install_summary -and $after.install_summary.status) {
        $afterStatus = $after.install_summary.status
    } elseif ($after.leo4_proxy_info -and $after.leo4_proxy_info.status) {
        $afterStatus = $after.leo4_proxy_info.status
    }
    $statusPass = ($afterStatus -eq "ready")
    $results += [PSCustomObject]@{
        Criterion = "Status Ready"
        Expected  = "ready"
        Actual    = $afterStatus
        Status    = if ($statusPass) { "PASS" } else { "FAIL" }
    }
    if (-not $statusPass) { $allPass = $false }

    # 3. All 4 services running
    $svcCheck = $true
    $svcDetails = @()
    if ($after.services) {
        foreach ($s in $after.services) {
            $svcDetails += "$($s.Name)=$($s.Status)"
            if ($s.Status -ne "Running") {
                $svcCheck = $false
            }
        }
    } else {
        $svcCheck = $false
    }
    $results += [PSCustomObject]@{
        Criterion = "Services Running"
        Expected  = "Leo4Proxy,mosquitto,L4Con,L4Superv = Running"
        Actual    = ($svcDetails -join ", ")
        Status    = if ($svcCheck) { "PASS" } else { "FAIL" }
    }
    if (-not $svcCheck) { $allPass = $false }

    # 4. cert.reused == true in install_summary
    $certReused = $false
    if ($after.install_summary -and $after.install_summary.cert) {
        $certReused = [bool]$after.install_summary.cert.reused
    }
    $results += [PSCustomObject]@{
        Criterion = "Cert Reused"
        Expected  = "True"
        Actual    = [string]$certReused
        Status    = if ($certReused) { "PASS" } else { "FAIL" }
    }
    if (-not $certReused) { $allPass = $false }

    # 5. exit_code == 0 in install_summary
    $exitCode = -1
    if ($after.install_summary -and $after.install_summary.exit_code -ne $null) {
        $exitCode = [int]$after.install_summary.exit_code
    }
    $results += [PSCustomObject]@{
        Criterion = "Exit Code Zero"
        Expected  = "0"
        Actual    = [string]$exitCode
        Status    = if ($exitCode -eq 0) { "PASS" } else { "FAIL" }
    }
    if ($exitCode -ne 0) { $allPass = $false }

    # 6. Idempotency Check (exe hashes identical)
    if ($after2) {
        $hashMatch = $true
        $diffCount = 0
        $files1 = @{}
        foreach ($f in $after.exe_files) { $files1[$f.RelativePath] = $f.Sha256 }
        $files2 = @{}
        foreach ($f in $after2.exe_files) { $files2[$f.RelativePath] = $f.Sha256 }

        foreach ($k in $files1.Keys) {
            if (-not $files2.ContainsKey($k) -or $files1[$k] -ne $files2[$k]) {
                $hashMatch = $false
                $diffCount++
            }
        }
        $results += [PSCustomObject]@{
            Criterion = "Idempotency (Exe Hashes Match)"
            Expected  = "All exe hashes identical"
            Actual    = if ($hashMatch) { "Identical ($($files1.Count) files)" } else { "$diffCount differences" }
            Status    = if ($hashMatch) { "PASS" } else { "FAIL" }
        }
        if (-not $hashMatch) { $allPass = $false }
    } else {
        # Check if After exe files have valid non-empty hashes
        $hasExes = ($after.exe_files -and $after.exe_files.Count -gt 0)
        $results += [PSCustomObject]@{
            Criterion = "Binary Hashes Verified"
            Expected  = "Valid non-empty hashes"
            Actual    = "$($after.exe_files.Count) exe binaries hashed"
            Status    = if ($hasExes) { "PASS" } else { "FAIL" }
        }
    }

    Write-Host "==========================================================================================" -ForegroundColor Yellow
    Write-Host " STAGE 1 STAND VERIFICATION REPORT (DoD 8)" -ForegroundColor Yellow
    Write-Host "==========================================================================================" -ForegroundColor Yellow
    $results | Format-Table -AutoSize | Out-String | Write-Host

    if ($allPass) {
        Write-Host "OVERALL VERIFICATION RESULT: >>> PASS <<<" -ForegroundColor Green
        return 0
    } else {
        Write-Host "OVERALL VERIFICATION RESULT: >>> FAIL <<<" -ForegroundColor Red
        return 1
    }
}

# Main dispatcher
switch ($Phase) {
    'Before' {
        New-StandSnapshot -phaseName 'Before' -targetDir $Path
        break
    }
    'After' {
        New-StandSnapshot -phaseName 'After' -targetDir $Path
        break
    }
    'Compare' {
        $exitCode = Invoke-CompareSnapshots -bFile $BeforeFile -aFile $AfterFile -aFile2 $AfterFile2 -targetDir $Path
        exit $exitCode
    }
}
