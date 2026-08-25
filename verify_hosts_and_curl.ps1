# Run PowerShell as administrator to update hosts directly or verify elevation
$hostsPath = "C:\Windows\System32\drivers\etc\hosts"
$content = [System.IO.File]::ReadAllText($hostsPath)
$lines = $content -split "`r?`n"
$filtered = @()
foreach ($l in $lines) {
    if ($l -notlike "*leo4*") {
        $filtered += $l
    }
}
$domains = @(
    "leo4-0000773.device.leo4.ru",
    "leo4-0000773.term.leo4.ru",
    "leo4-0000773.internal",
    "leo4-0000773.local"
)
foreach ($d in $domains) {
    $filtered += "192.168.1.127`t$d`t# Leo4Proxy LAN alias"
    $filtered += "127.0.0.1`t$d`t# Leo4Proxy local alias"
    $filtered += "::1`t$d`t# Leo4Proxy local alias IPv6"
}
$newText = ($filtered -join "`r`n") + "`r`n"
[System.IO.File]::WriteAllText($hostsPath, $newText, [System.Text.Encoding]::ASCII)
Write-Host "Hosts written!"

$p = Start-Process -FilePath "D:\repo\platerra\Public\etranprocessing\tools\leo4proxy\bin\leo4proxy.exe" -ArgumentList "-f", "-v" -PassThru
Start-Sleep -Seconds 2

Write-Host "=== TEST 1: device.leo4.ru ==="
& curl.exe -k -i "https://leo4-0000773.device.leo4.ru/_leo4/info" | Select-Object -First 5

Write-Host "=== TEST 2: term.leo4.ru ==="
& curl.exe -k -i "https://leo4-0000773.term.leo4.ru/_leo4/info" | Select-Object -First 5

Write-Host "=== TEST 3: internal ==="
& curl.exe -k -i "https://leo4-0000773.internal/_leo4/info" | Select-Object -First 5

Write-Host "=== TEST 4: local ==="
& curl.exe -k -i "https://leo4-0000773.local/_leo4/info" | Select-Object -First 5

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
