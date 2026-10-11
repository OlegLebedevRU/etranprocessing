[CmdletBinding()]
param(
    [string]$ManifestPath = 'D:\esp32-workplace\sip\build\acceptance\camera-panel-cleanup.json',
    [switch]$PrepareManifest,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
if ($PrepareManifest -and $Execute) { throw 'Prepare and execution must be separate runs.' }

# Only temporary files created by the camera-panel task. Keep firmware, .env,
# acceptance JSON/logs/screenshots, dependencies and all other worktrees.
$temporaryRoots = @(
    'D:\repo\platerra\Public\etranprocessing-media-expiry\MenuBuilder\backend\.pytest_tmp',
    'D:\esp32-workplace\sip\build\acceptance\camera-panel-live.cjs',
    'D:\esp32-workplace\sip\build\acceptance\camera-panel-final-read.cjs',
    'D:\esp32-workplace\sip\build\acceptance\camera-panel-busy.py',
    'D:\esp32-workplace\sip\build\acceptance\camera-panel-pr.md',
    'D:\esp32-workplace\sip\build\acceptance\camera-panel-followup-pr.md',
    'D:\esp32-workplace\sip\build\acceptance\camera-panel-completed-pr.md',
    'D:\esp32-workplace\sip\build\acceptance\base-sync-user-diff-before.patch',
    'D:\esp32-workplace\sip\build\acceptance\base-sync-user-diff-after.patch'
)

function FullPath([string]$Path) { return [IO.Path]::GetFullPath($Path).TrimEnd('\', '/') }

function Assert-Root([string]$Path) {
    $full = FullPath $Path
    if ($temporaryRoots -notcontains $full) { throw "Unapproved cleanup target: $full" }
    $cursor = $full
    while ($cursor) {
        $item = Get-Item -LiteralPath $cursor -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Target or parent is a reparse point: $cursor"
        }
        $parent = [IO.Directory]::GetParent($cursor)
        $cursor = if ($parent) { $parent.FullName } else { $null }
    }
    return $full
}

function Get-Snapshot([string]$Path) {
    $root = Assert-Root $Path
    $stack = [Collections.Generic.Stack[string]]::new()
    $stack.Push($root)
    $entries = [Collections.Generic.List[object]]::new()
    while ($stack.Count) {
        $node = $stack.Pop()
        $item = Get-Item -LiteralPath $node -Force
        $relative = if ($node -eq $root) { '.' } else { $node.Substring($root.Length + 1) }
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            # pytest's *current links are removed as links, never traversed.
            if ($item.LinkType -notin @('SymbolicLink', 'Junction')) { throw "Unknown reparse point: $node" }
            $target = @($item.Target)
            if ($target.Count -ne 1) { throw "Unexpected link target: $node" }
            $resolved = if ([IO.Path]::IsPathRooted($target[0])) { FullPath $target[0] } else {
                FullPath (Join-Path ([IO.Path]::GetDirectoryName($node)) $target[0])
            }
            if (-not $resolved.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
                throw "Link leaves the temporary directory: $node"
            }
            $entries.Add([ordered]@{ relative = $relative; kind = 'link'; target = [string]$target[0] })
        } elseif ($item.PSIsContainer) {
            $entries.Add([ordered]@{ relative = $relative; kind = 'directory' })
            foreach ($child in @(Get-ChildItem -LiteralPath $node -Force)) { $stack.Push($child.FullName) }
        } else {
            $entries.Add([ordered]@{ relative = $relative; kind = 'file'; length = $item.Length;
                sha256 = (Get-FileHash -LiteralPath $node -Algorithm SHA256).Hash })
        }
    }
    return @($entries | Sort-Object { $_.relative })
}

$manifestFile = FullPath $ManifestPath
if ($temporaryRoots -contains $manifestFile) { throw 'Manifest cannot be a cleanup target.' }
if ($PrepareManifest) {
    if (Test-Path -LiteralPath $manifestFile) { throw 'Manifest already exists; review it before replacing it.' }
    $targets = @(foreach ($path in $temporaryRoots) {
        if (Test-Path -LiteralPath $path) {
            [ordered]@{ path = (FullPath $path); entries = @(Get-Snapshot $path) }
        }
    })
    [ordered]@{ schema = 1; created_at = [DateTime]::UtcNow.ToString('o'); targets = $targets } |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestFile -Encoding UTF8
    Write-Host "Manifest prepared: $manifestFile"
    Write-Host 'Run without switches to review the exact targets.'
    return
}

$manifest = Get-Content -LiteralPath $manifestFile -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schema -ne 1) { throw 'Unsupported manifest schema.' }
$seen = @{}
# Validate every target before deleting anything. Missing/already removed roots
# are skipped; a partially changed target still requires a new review.
$validated = @(foreach ($candidate in @($manifest.targets)) {
    $path = FullPath $candidate.path
    if ($temporaryRoots -notcontains $path -or $seen.ContainsKey($path)) { throw "Invalid manifest target: $path" }
    $seen[$path] = $true
    if (-not (Test-Path -LiteralPath $path)) { Write-Host "Already absent: $path"; continue }
    $actual = @(Get-Snapshot $path)
    $expected = @($candidate.entries)
    if (($actual | ConvertTo-Json -Depth 6 -Compress) -cne ($expected | ConvertTo-Json -Depth 6 -Compress)) {
        throw "Contents changed since review: $path. Nothing deleted."
    }
    [PSCustomObject]@{ path = $path; entries = $actual }
})

foreach ($candidate in $validated) {
    $files = @($candidate.entries | Where-Object { $_.kind -eq 'file' })
    [long]$bytes = 0
    foreach ($file in $files) { $bytes += [long]$file['length'] }
    Write-Host "$($candidate.path): $($files.Count) files, $bytes bytes"
}
if (-not $Execute) { Write-Host 'Preview only. Use -Execute to delete these reviewed targets.'; return }

foreach ($candidate in $validated) {
    [void](Assert-Root $candidate.path)
    # Recheck immediately before mutation; no shell handoff or recursive delete.
    if ((@(Get-Snapshot $candidate.path) | ConvertTo-Json -Depth 6 -Compress) -cne
        ($candidate.entries | ConvertTo-Json -Depth 6 -Compress)) { throw "Target changed: $($candidate.path)" }
    foreach ($entry in @($candidate.entries | Sort-Object { $_.relative.Length } -Descending)) {
        $path = if ($entry.relative -eq '.') { $candidate.path } else { Join-Path $candidate.path $entry.relative }
        $full = FullPath $path
        if ($full -ne $candidate.path -and -not $full.StartsWith($candidate.path + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Path escaped approved target: $full"
        }
        # Directories must be empty. Never descend into a junction or symlink.
        if ($entry.kind -eq 'directory' -and @(Get-ChildItem -LiteralPath $full -Force).Count) {
            throw "Directory is not empty: $full"
        }
        Remove-Item -LiteralPath $full -Force
    }
    if (Test-Path -LiteralPath $candidate.path) { throw "Cleanup incomplete: $($candidate.path)" }
}
Write-Host 'Camera-panel temporary files removed. Acceptance evidence and repositories retained.'
