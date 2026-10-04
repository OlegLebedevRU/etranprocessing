# Lists task residue by default. Run with -Execute to remove reviewed targets.
# Source files, Docker files, certificates and remote backups are outside this list.
[CmdletBinding()]
param([switch]$Execute)

$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\', '/')
$taskPrefix = $taskRoot + [IO.Path]::DirectorySeparatorChar

if ((Split-Path $taskRoot -Leaf) -ne 'l4desk-landing' -or
    -not (Test-Path -LiteralPath (Join-Path $taskRoot 'site/index.html') -PathType Leaf)) {
    throw 'Run this script from its original l4desk-landing directory.'
}

# Refuse junctions/symlinks, including those in the project ancestors.
$ancestor = Get-Item -LiteralPath $taskRoot -Force
while ($null -ne $ancestor) {
    if ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Cleanup root passes through a reparse point: $($ancestor.FullName)"
    }
    $ancestor = $ancestor.Parent
}

$reviewedDirectories = @(
    '.preview-chrome', '.preview-chrome2', '.preview-edge',
    '.preview-edge-500', '.preview-edge-500b', '.preview-edge-mobile',
    '.preview-edge-mobile2', '.preview-edge-tall'
)
$reviewedFiles = [ordered]@{
    '.deploy-landing-site.tar.gz' = '64288A158AF5C5635667C3843A5BD4F0B5A862071A14B6E1163B6B9E22C9C5DE'
    'preview-500.png' = '0BE57ADF75C0AED25FC30AC5E12E4B945C69F962462A6A538CB807C49724256A'
    'preview-desktop.png' = '5CFA817CA1FA6B8F27961D57FFBD0A53A3E48ACDFD6F34897965EF8308050BEF'
    'preview-mobile.png' = 'C73E9049A780EAA38B5B1CBB925B1581E2EDFA0CD5650188B1E050DB19888820'
    'preview-tall.png' = '3463CB0DC5723C74453520BE70AB391D8E242CC0445A52C5C56EB2D2B43F4860'
}

function Get-TargetSummary {
    param([IO.FileSystemInfo]$Item)
    $pending = [Collections.Generic.Queue[IO.FileSystemInfo]]::new()
    $pending.Enqueue($Item)
    [long]$bytes = 0
    [long]$files = 0
    while ($pending.Count -gt 0) {
        $entry = $pending.Dequeue()
        if (-not $entry.FullName.StartsWith($taskPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Target is outside the project: $($entry.FullName)"
        }
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing to traverse a reparse point: $($entry.FullName)"
        }
        if ($entry.PSIsContainer) {
            foreach ($child in Get-ChildItem -LiteralPath $entry.FullName -Force) {
                $pending.Enqueue($child)
            }
        } else {
            $bytes += $entry.Length
            $files++
        }
    }
    [pscustomobject]@{
        Path = $Item.FullName
        Directory = [bool]$Item.PSIsContainer
        Files = $files
        MiB = [Math]::Round($bytes / 1MB, 2)
    }
}

# Validate every target before deleting any of them.
$targets = @(
    foreach ($name in @($reviewedDirectories) + @($reviewedFiles.Keys)) {
        $path = [IO.Path]::GetFullPath((Join-Path $taskRoot $name))
        if (-not $path.StartsWith($taskPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Target is outside the project: $path"
        }
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $item = Get-Item -LiteralPath $path -Force
        if ([bool]$item.PSIsContainer -ne ($reviewedDirectories -contains $name)) {
            throw "Target type changed: $path"
        }
        $summary = Get-TargetSummary -Item $item
        if ($reviewedFiles.Contains($name) -and
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $reviewedFiles[$name]) {
            throw "File changed since review; nothing has been removed: $path"
        }
        $summary
    }
)

if ($targets.Count -eq 0) {
    Write-Output 'No task temporary files remain.'
    return
}
$targets | Format-Table Path, Files, MiB -AutoSize
if (-not $Execute) {
    Write-Output 'Preview only. Run .\Clear-LandingTemp.ps1 -Execute to remove these targets.'
    return
}

foreach ($target in $targets) {
    # Recheck the tree immediately before each native PowerShell deletion.
    $null = Get-TargetSummary -Item (Get-Item -LiteralPath $target.Path -Force)
    if (-not $target.Directory -and
        (Get-FileHash -LiteralPath $target.Path -Algorithm SHA256).Hash -ne
        $reviewedFiles[(Split-Path $target.Path -Leaf)]) {
        throw "File changed during cleanup: $($target.Path)"
    }
    Remove-Item -LiteralPath $target.Path -Recurse:$target.Directory -Force
    Write-Output "Removed: $($target.Path)"
}
