#requires -Version 5.1
[CmdletBinding()]
param([switch]$Apply)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..')).TrimEnd('\')

function Test-CleanupCandidate([string]$Relative) {
    if ($Relative -match '^tools/dist/\.release/') {
        $tail = $Relative.Substring('tools/dist/.release/'.Length)
        if ($tail -match '^(install-kit|evidence|diagnostics|\d+\.\d+\.\d+)(/|$)' -or $tail -match '^installer-ready-' -or $tail -in @('Continue-Fresh773.cmd', 'diagnostic-pin-773.txt', 'workspace.lock') -or $tail -match '^cleanup.*\.json$') { return $false }
        return $true
    }
    if ($Relative -match '^tools/dist/\.stage/') { return $true }
    if ($Relative -match '^tools/[a-z0-9_-]+/(bin|obj)/') { return $Relative -notmatch '^tools/l4rollback/bin/' }
    if ($Relative -match '^tools/l4setup/res/(payload\.rc|version\.h|[^/]+\.bin)$') { return $true }
    if ($Relative -match '^(tools|l4release)/' -and $Relative -notmatch '^tools/(\.venv|[^/]+/vendor)/' -and $Relative -match '/(__pycache__|\.pytest_cache)/') { return $true }
    return $Relative -match '^tools/[a-z0-9_-]+/[^/]+\.(obj|pdb)$'
}
function Assert-WorkspacePath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Cleanup path escaped this worktree.' }
    $item = Get-Item -LiteralPath $full -Force
    while ($item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Cleanup refuses reparse paths.' }
        if ($item.FullName -ieq $root) { break }
        if ($item -is [IO.FileInfo]) { $item = $item.Directory } else { $item = $item.Parent }
    }
}
Push-Location $root
$cleanupLock = $null
$lockAcquired = $false
try {
    if ($Apply) {
        $lockPath = Join-Path $root 'tools\dist\.release\workspace.lock'
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($lockPath)) | Out-Null
        Assert-WorkspacePath ([IO.Path]::GetDirectoryName($lockPath))
        if (Test-Path -LiteralPath $lockPath) { Assert-WorkspacePath $lockPath }
        $cleanupLock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::ReadWrite)
        # Same byte-range lock as l4release.runner.workspace_lock (msvcrt).
        $cleanupLock.Lock(0, 1)
        $lockAcquired = $true
    }
    $ignored = @(& git -c core.quotepath=false ls-files --others --ignored --exclude-standard -- tools l4release)
    if ($LASTEXITCODE -ne 0) { throw 'Git ignored inventory failed.' }
    $plan = @()
    foreach ($relative in $ignored) {
        if (-not (Test-CleanupCandidate $relative)) { continue }
        if ($relative -match '[\x00-\x1f:]') { throw 'Unsafe cleanup path.' }
        $path = [IO.Path]::GetFullPath((Join-Path $root $relative))
        Assert-WorkspacePath $path
        $file = Get-Item -LiteralPath $path -Force
        if ($file.PSIsContainer) { throw 'Git inventory unexpectedly contains a directory.' }
        $plan += [pscustomobject]@{ Relative = $relative; Path = $path; Bytes = $file.Length }
    }
    $bytes = [long]0
    foreach ($file in $plan) { $bytes += $file.Bytes }
    [pscustomobject]@{ Mode = $(if ($Apply) { 'apply' } else { 'preview' }); Files = $plan.Count; MiB = [Math]::Round($bytes / 1MB, 1) } | Format-Table -AutoSize
    if (-not $plan.Count) { Write-Output 'No matching build artifacts remain.'; return }
    $plan | Group-Object { ($_.Relative -split '/')[0..2] -join '/' } | ForEach-Object {
        [pscustomobject]@{ Area = $_.Name; Files = $_.Count; MiB = [Math]::Round(($_.Group | Measure-Object Bytes -Sum).Sum / 1MB, 1) }
    } | Sort-Object MiB -Descending | Format-Table -AutoSize
    if (-not $Apply) { return }

    # Recheck the Git index before mutation; a newly tracked file must survive.
    $tracked = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $names = @(& git -c core.quotepath=false ls-files --cached -- tools l4release)
    if ($LASTEXITCODE -ne 0) { throw 'Git tracked inventory failed.' }
    foreach ($name in $names) { $null = $tracked.Add($name) }
    $parents = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $deleted = @(); $failures = @()
    foreach ($file in $plan) {
        if ($tracked.Contains($file.Relative)) { throw 'Cleanup candidate became tracked; stopping.' }
        Assert-WorkspacePath $file.Path
        try {
            Remove-Item -LiteralPath $file.Path -Force
            $deleted += $file
            $parent = [IO.Path]::GetDirectoryName($file.Path)
            while ($parent.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
                $relative = $parent.Substring($root.Length + 1).Replace('\', '/')
                if (-not (Test-CleanupCandidate ($relative + '/__cleanup__'))) { break }
                $null = $parents.Add($parent)
                $parent = [IO.Path]::GetDirectoryName($parent)
            }
        } catch { $failures += [pscustomobject]@{ Path = $file.Relative; Error = $_.Exception.Message } }
    }
    foreach ($parent in $parents | Sort-Object Length -Descending) {
        if (-not (Test-Path -LiteralPath $parent)) { continue }
        Assert-WorkspacePath $parent
        if (-not @(Get-ChildItem -LiteralPath $parent -Force).Count) {
            try { [IO.Directory]::Delete($parent, $false) }
            catch { Write-Warning ('Empty directory retained: ' + $parent) }
        }
    }
    $report = Join-Path $root 'tools\dist\.release\cleanup-last.json'
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($report)) | Out-Null
    $removedBytes = [long]0
    foreach ($file in $deleted) { $removedBytes += $file.Bytes }
    [pscustomobject]@{ TimestampUtc = [DateTime]::UtcNow.ToString('o'); Worktree = $root; RemovedFiles = $deleted.Count; RemovedBytes = $removedBytes; Failures = $failures; Removed = @($deleted | ForEach-Object { $_.Relative }) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $report -Encoding UTF8
    "Removed $($deleted.Count) files; retained $($failures.Count) files with errors. Report: $report"
    if ($failures.Count) { throw 'Cleanup incomplete; inspect the retained-file report.' }
} finally {
    if ($cleanupLock) {
        try { if ($lockAcquired) { $cleanupLock.Unlock(0, 1) } }
        finally { $cleanupLock.Dispose() }
    }
    Pop-Location
}
