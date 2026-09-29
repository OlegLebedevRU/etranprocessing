[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'

function FullPath([string]$Value) {
    return [IO.Path]::GetFullPath($Value).TrimEnd('\', '/')
}

function Assert-ChildPath([string]$Value, [string]$Parent) {
    $full = FullPath $Value
    $root = FullPath $Parent
    if (-not $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the approved root: $full"
    }
    if (-not (Test-Path -LiteralPath $full)) { throw "Path disappeared: $full" }
    $cursor = $full
    while ($cursor.Length -ge $root.Length) {
        $item = Get-Item -LiteralPath $cursor -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Reparse point is not eligible for cleanup: $cursor"
        }
        if ($cursor -eq $root) { break }
        $cursor = [IO.Directory]::GetParent($cursor).FullName.TrimEnd('\', '/')
    }
    return $full
}

function Git-Output([string]$Directory, [string[]]$GitArgs) {
    $value = & git -C $Directory @GitArgs 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Git failed in $Directory`: git $($GitArgs -join ' ')" }
    return (@($value) -join "`n").Trim()
}

function Assert-CleanGit([string]$Directory) {
    $state = Git-Output $Directory @('status', '--porcelain=v1', '--untracked-files=all')
    if ($state) { throw "Git checkout is not clean: $Directory" }
}

$manifestFile = FullPath $ManifestPath
if (-not (Test-Path -LiteralPath $manifestFile -PathType Leaf)) { throw "Manifest not found: $manifestFile" }
$manifest = Get-Content -LiteralPath $manifestFile -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schema -ne 1) { throw 'Unsupported manifest schema' }
$repo = FullPath $manifest.repository_root
if (-not (Test-Path -LiteralPath (Join-Path $repo '.git'))) { throw "Repository not found: $repo" }
$remote = Git-Output $repo @('config', '--get', 'remote.origin.url')
if ($remote -ne 'https://github.com/OlegLebedevRU/etranprocessing.git') {
    throw "Unexpected repository remote: $remote"
}
$base = Git-Output $repo @('rev-parse', 'origin/main')
$published = (& git -C $repo ls-remote origin refs/heads/main 2>&1)
if ($LASTEXITCODE -ne 0 -or -not $published) { throw 'Cannot verify origin/main on GitHub' }
$remoteSha = ([string]$published -split '\s+')[0]
if ($base -ne $remoteSha) { throw 'origin/main is stale; run git fetch origin main before cleanup' }
if ($base -ne $manifest.reviewed_main_sha) {
    throw "main changed since review: $base"
}

$mainCommon = FullPath (Git-Output $repo @('rev-parse', '--path-format=absolute', '--git-common-dir'))
$approvedWorktreeRoot = FullPath $manifest.worktree_root
$approvedTempRoot = FullPath $manifest.temp_root
$codexRoot = FullPath $manifest.codex_root
$worktreePaths = @()
$clonePaths = @()
$filePaths = @()
$directoryPaths = @()

foreach ($candidate in @($manifest.worktrees)) {
    $path = FullPath $candidate.path
    $parent = if ($path.StartsWith($approvedWorktreeRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        $approvedWorktreeRoot
    } else { $approvedTempRoot }
    $path = Assert-ChildPath $path $parent
    if (-not (Test-Path -LiteralPath $path -PathType Container)) { throw "Not a directory: $path" }
    if ((FullPath (Git-Output $path @('rev-parse', '--path-format=absolute', '--git-common-dir'))) -ne $mainCommon) {
        throw "Not a linked worktree of the reviewed repository: $path"
    }
    if ((Git-Output $path @('rev-parse', 'HEAD')) -ne $candidate.head) { throw "Worktree HEAD changed: $path" }
    if ((Git-Output $path @('branch', '--show-current')) -ne [string]$candidate.branch) {
        throw "Worktree branch changed: $path"
    }
    Assert-CleanGit $path
    & git -C $repo merge-base --is-ancestor $candidate.head origin/main 2>$null
    if ($LASTEXITCODE -ne 0) { throw "Worktree commit is not merged into main: $path" }
    $worktreePaths += $path
}

foreach ($candidate in @($manifest.clones)) {
    $path = Assert-ChildPath $candidate.path $approvedWorktreeRoot
    if (-not (Test-Path -LiteralPath (Join-Path $path '.git') -PathType Container)) {
        throw "Not an independent clone: $path"
    }
    if ((Git-Output $path @('config', '--get', 'remote.origin.url')) -ne $remote) {
        throw "Clone remote differs: $path"
    }
    if ((Git-Output $path @('rev-parse', 'HEAD')) -ne $candidate.head) { throw "Clone HEAD changed: $path" }
    if ((Git-Output $path @('rev-parse', 'HEAD^{tree}')) -ne $candidate.tree) {
        throw "Clone tree changed: $path"
    }
    & git -C $repo merge-base --is-ancestor $candidate.base_commit origin/main 2>$null
    if ($LASTEXITCODE -ne 0 -or
        (Git-Output $repo @('rev-parse', "$($candidate.base_commit)^{tree}")) -ne $candidate.tree) {
        throw "Clone tree is not the reviewed base tree: $path"
    }
    Assert-CleanGit $path
    $clonePaths += $path
}

$checkedOut = @($manifest.worktrees | Where-Object { $_.branch } | ForEach-Object { [string]$_.branch })
foreach ($candidate in @($manifest.branches)) {
    $branch = [string]$candidate.name
    if (-not $branch -or $branch -eq 'main' -or $branch.StartsWith('-')) {
        throw "Protected or invalid branch: $branch"
    }
    if ((Git-Output $repo @('rev-parse', "refs/heads/$branch")) -ne $candidate.head) {
        throw "Branch moved since review: $branch"
    }
    & git -C $repo merge-base --is-ancestor $candidate.head origin/main 2>$null
    if ($LASTEXITCODE -ne 0) { throw "Branch not merged into main: $branch" }
    if ($checkedOut -notcontains $branch) {
        $listed = & git -C $repo worktree list --porcelain
        if (@($listed | Where-Object { $_ -eq "branch refs/heads/$branch" }).Count) {
            throw "Branch remains checked out in another worktree: $branch"
        }
    }
}

foreach ($candidate in @($manifest.codex_files)) {
    $path = Assert-ChildPath $candidate.path $codexRoot
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Not a file: $path" }
    $item = Get-Item -LiteralPath $path -Force
    if ($item.Length -ne $candidate.length -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $candidate.sha256) {
        throw "File changed since review: $path"
    }
    $filePaths += $path
}

foreach ($candidate in @($manifest.codex_directories)) {
    $path = Assert-ChildPath $candidate.path $codexRoot
    if (-not (Test-Path -LiteralPath $path -PathType Container) -or
        (Test-Path -LiteralPath (Join-Path $path '.git'))) {
        throw "Directory is not an eligible temporary folder: $path"
    }
    $all = @(Get-ChildItem -LiteralPath $path -Recurse -Force)
    if (@($all | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
        throw "Directory contains a reparse point: $path"
    }
    $actual = @($all | Where-Object { -not $_.PSIsContainer } | Sort-Object FullName)
    $expected = @($candidate.files | Sort-Object relative)
    $actualDirectories = @($all | Where-Object { $_.PSIsContainer } |
        ForEach-Object { $_.FullName.Substring($path.Length + 1).Replace('\', '/') } | Sort-Object)
    $expectedDirectories = @($candidate.directories | Sort-Object)
    if (($actualDirectories -join "`n") -ne ($expectedDirectories -join "`n")) {
        throw "Directory structure changed: $path"
    }
    if ($actual.Count -ne $expected.Count) { throw "Directory file count changed: $path" }
    for ($i = 0; $i -lt $actual.Count; $i++) {
        $relative = $actual[$i].FullName.Substring($path.Length + 1).Replace('\', '/')
        if ($relative -ne $expected[$i].relative -or
            $actual[$i].Length -ne $expected[$i].length -or
            (Get-FileHash -LiteralPath $actual[$i].FullName -Algorithm SHA256).Hash -ne $expected[$i].sha256) {
            throw "Directory content changed: $path"
        }
    }
    $directoryPaths += $path
}

Write-Host "Reviewed main: $base"
Write-Host "Linked worktrees: $($worktreePaths.Count); independent clones: $($clonePaths.Count); local branches: $(@($manifest.branches).Count)"
Write-Host "D:\.codex files: $($filePaths.Count); directories: $($directoryPaths.Count)"
foreach ($path in ($worktreePaths + $clonePaths + $filePaths + $directoryPaths)) { Write-Host "  $path" }
if (-not $Execute) { Write-Host 'Preview only. Re-run with -Execute to remove these exact reviewed targets.'; return }

foreach ($path in $worktreePaths) {
    & git -C $repo worktree remove $path
    if ($LASTEXITCODE -ne 0) { throw "Worktree removal failed: $path" }
}
foreach ($candidate in @($manifest.branches)) {
    & git -C $repo branch -D -- $candidate.name
    if ($LASTEXITCODE -ne 0) { throw "Branch removal failed: $($candidate.name)" }
}
foreach ($path in $filePaths) { Remove-Item -LiteralPath $path -Force }
foreach ($path in ($directoryPaths + $clonePaths)) {
    $parent = if ($directoryPaths -contains $path) { $codexRoot } else { $approvedWorktreeRoot }
    [void](Assert-ChildPath $path $parent)
    Remove-Item -LiteralPath $path -Recurse -Force
}
Write-Host 'Reviewed local cleanup completed. Remote branches and the current checkout were not changed.'
