param([Parameter(Mandatory=$true)][string]$OutputFile, [int]$DurationSeconds = 1800)
$ErrorActionPreference = 'Stop'
$deadline = [DateTime]::UtcNow.AddSeconds($DurationSeconds)
$path = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputFile)
$stream = [IO.File]::Open($path, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
$writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false))
$writer.AutoFlush = $true
try {
  while ([DateTime]::UtcNow -lt $deadline) {
    $cpu = Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'"
    $processes = @(Get-Process chrome,l4capture,l4desk,leo4proxy -ErrorAction SilentlyContinue | ForEach-Object { @{ pid = $_.Id; name = $_.ProcessName; cpuSeconds = $_.CPU; workingSet = $_.WorkingSet64 } })
    $gpu = $null
    try {
        $gpu = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine | Where-Object { $_.Name -match 'engtype_(VideoDecode|VideoEncode|3D)' } | ForEach-Object { @{ engine = $_.Name; utilization = $_.UtilizationPercentage } })
    } catch { $gpu = $null }
    $line = @{ utc = [DateTime]::UtcNow.ToString('o'); cpuPercent = $cpu.PercentProcessorTime; logicalProcessors = [Environment]::ProcessorCount; processes = $processes; gpuEngines = $gpu } | ConvertTo-Json -Compress -Depth 5
    $writer.WriteLine($line)
    Start-Sleep -Seconds 5
  }
} finally {
    $writer.Dispose()
}
