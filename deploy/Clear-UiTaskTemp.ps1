[CmdletBinding(SupportsShouldProcess = $true)]
param([switch]$Execute)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Only disposable caches created by the MenuBuilder UI task are eligible.
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
$names = @('mb-ui-npm-cache', 'mb-ui-uv-cache')
$targets = @()

foreach ($name in $names) {
    $target = [IO.Path]::GetFullPath((Join-Path $tempRoot $name))
    if ([IO.Directory]::GetParent($target).FullName.TrimEnd('\', '/') -ne $tempRoot) {
        throw "Target is outside the temporary root: $target"
    }
    if (-not (Test-Path -LiteralPath $target)) { continue }
    if (-not (Test-Path -LiteralPath $target -PathType Container)) {
        throw "Expected a cache directory: $target"
    }

    # Check ancestors and every child without following directory links.
    $cursor = $target
    while ($cursor) {
        $item = Get-Item -LiteralPath $cursor -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing a link or reparse point: $cursor"
        }
        $parent = [IO.Directory]::GetParent($cursor)
        $cursor = if ($null -eq $parent) { $null } else { $parent.FullName }
    }
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($target)
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        foreach ($child in Get-ChildItem -LiteralPath $directory -Force) {
            if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing a cache containing a link: $($child.FullName)"
            }
            if ($child.PSIsContainer) { $pending.Push($child.FullName) }
        }
    }
    $targets += $target
}

# Validate all targets before deleting any. Stop builds and package installs first.
if ($targets.Count -eq 0) { Write-Output 'No task caches found.' }
foreach ($target in $targets) {
    if (-not $Execute) {
        Write-Output "Preview: $target"
    } elseif ($PSCmdlet.ShouldProcess($target, 'Remove temporary task cache')) {
        Remove-Item -LiteralPath $target -Recurse -Force
        Write-Output "Removed: $target"
    }
}
if (-not $Execute) { Write-Output 'Run with -Execute to delete the listed caches.' }
