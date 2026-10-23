[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$RepairAcl
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
$sshRoot = 'D:\.ssh'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$currentSid = '*' + $identity.User.Value

# Exact task-owned targets. Never remove the shared temporary root.
$targets = @(
    @{ Path = "$sshRoot\id_ed25519_codex_jwt_task"; Root = $sshRoot; Kind = 'File' },
    @{ Path = "$sshRoot\free-tier-cloud_ru_codex_jwt_task"; Root = $sshRoot; Kind = 'File' },
    @{ Path = "$repoRoot\.release-test-tmp\jwt-signer"; Root = $repoRoot; Kind = 'Directory' },
    @{ Path = "$repoRoot\.release-test-tmp\etran-jwt-native-release"; Root = $repoRoot; Kind = 'Clone'; Remote = 'https://github.com/OlegLebedevRU/etranprocessing.git'; Head = 'c856a962fe93e7a499f6b261ff72b90af5e08d50' },
    @{ Path = "$repoRoot\.release-test-tmp\platerra-native"; Root = $repoRoot; Kind = 'Clone'; Remote = 'https://github.com/OlegLebedevRU/platerra.ru.git'; Head = '30f973ba865cbd9af316d569e3978e79c8182c6b' },
    @{ Path = "$repoRoot\MenuBuilder\backend\.pytest_tmp_jwt_signer"; Root = $repoRoot; Kind = 'Directory' },
    @{ Path = "$repoRoot\MenuBuilder\backend\.pytest_tmp_jwt_signer_full"; Root = $repoRoot; Kind = 'Directory' }
)

function Assert-TargetPath($Target) {
    $path = [IO.Path]::GetFullPath($Target.Path).TrimEnd('\')
    $root = [IO.Path]::GetFullPath($Target.Root).TrimEnd('\')
    if (-not $path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Target escapes its approved root: $path"
    }
    if ($path -ne $Target.Path -or $path -eq $repoRoot -or $path -eq $sshRoot) {
        throw "Unexpected cleanup path: $path"
    }
    $workingDirectory = [IO.Path]::GetFullPath((Get-Location).ProviderPath).TrimEnd('\')
    if ($Target.Kind -ne 'File' -and ($workingDirectory -eq $path -or
        $workingDirectory.StartsWith($path + '\', [StringComparison]::OrdinalIgnoreCase))) {
        throw "Change working directory before removing this target: $path"
    }
    $cursor = $path
    while ($cursor) {
        $item = Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing a junction or symlink: $cursor"
        }
        $parent = [IO.Directory]::GetParent($cursor)
        if ($null -eq $parent) { break }
        $cursor = $parent.FullName
    }
    $item = Get-Item -LiteralPath $path -Force
    if (($Target.Kind -eq 'File') -eq $item.PSIsContainer) {
        throw "Unexpected target type: $path"
    }
}

function Assert-DirectoryTree([string]$Path) {
    # PowerShell does not follow directory links here; reject any discovered link.
    $items = @(Get-ChildItem -LiteralPath $Path -Recurse -Force -ErrorAction Stop)
    foreach ($item in $items) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing a directory containing a link: $($item.FullName)"
        }
    }
}

function Git-Output([string]$Path, [string[]]$Arguments) {
    $output = & git -c "safe.directory=$($Path.Replace('\', '/'))" -c core.fsmonitor=false -C $Path @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Git check failed in $Path ($($Arguments -join ' ')): $($output -join ' ')" }
    return ($output -join "`n").Trim()
}

function Assert-PublishedClone($Target) {
    if ((Git-Output $Target.Path @('config', '--get', 'remote.origin.url')) -ne $Target.Remote) {
        throw "Unexpected clone remote: $($Target.Path)"
    }
    if ((Git-Output $Target.Path @('rev-parse', 'HEAD')) -ne $Target.Head) {
        throw "Clone HEAD changed since review: $($Target.Path)"
    }
    if (Git-Output $Target.Path @('status', '--porcelain=v1', '--untracked-files=all')) {
        throw "Clone has uncommitted files: $($Target.Path)"
    }
    # Fetch only in the disposable clone, never reset a branch or working files.
    [void](Git-Output $Target.Path @('fetch', '--quiet', 'origin', 'main'))
    [void](Git-Output $Target.Path @('merge-base', '--is-ancestor', $Target.Head, 'FETCH_HEAD'))
    $refs = (Git-Output $Target.Path @('for-each-ref', '--format=%(refname)', 'refs/heads')) -split "`n"
    foreach ($ref in $refs) {
        if ($ref) { [void](Git-Output $Target.Path @('merge-base', '--is-ancestor', $ref, 'FETCH_HEAD')) }
    }
    $remoteTags = @{}
    foreach ($line in ((Git-Output $Target.Path @('ls-remote', '--tags', '--refs', 'origin')) -split "`n")) {
        if ($line) { $parts = $line -split '\s+'; $remoteTags[$parts[1]] = $parts[0] }
    }
    foreach ($tag in ((Git-Output $Target.Path @('for-each-ref', '--format=%(refname)', 'refs/tags')) -split "`n")) {
        if ($tag -and (Git-Output $Target.Path @('rev-parse', $tag)) -ne $remoteTags[$tag]) {
            throw "Clone contains an unpublished or changed tag: $tag"
        }
    }
}

function Repair-TargetAcl($Target) {
    # Take ownership before WRITE_DAC operations. Walk one directory at a time
    # so inaccessible trees never cause recursive tools to follow an unseen link.
    # No permissions are changed on .ssh, the repository root, or original keys.
    $queue = New-Object 'System.Collections.Generic.Queue[string]'
    $queue.Enqueue($Target.Path)
    while ($queue.Count -gt 0) {
        $path = $queue.Dequeue()
        $full = [IO.Path]::GetFullPath($path).TrimEnd('\')
        if ($full -ne $Target.Path -and -not $full.StartsWith($Target.Path + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "ACL repair target escapes its temporary directory: $full"
        }
        $item = Get-Item -LiteralPath $full -Force -ErrorAction Stop
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing ACL repair on a junction or symlink: $full"
        }
        # No /R or /D: no recursion, wildcard, or localized confirmation prompt.
        & takeown /F $full | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Cannot take ownership: $full. Check elevated account privileges." }
        & icacls $full /grant:r "${currentSid}:(F)" /L | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Cannot repair ACL: $full" }
        if ($item.PSIsContainer) {
            foreach ($child in @(Get-ChildItem -LiteralPath $full -Force -ErrorAction Stop)) {
                if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw "Refusing a directory containing a link: $($child.FullName)"
                }
                $queue.Enqueue($child.FullName)
            }
        }
    }
}

if ($RepairAcl -and -not $Execute) {
    throw '-RepairAcl requires -Execute. Preview never changes permissions.'
}
if ($RepairAcl) {
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'PowerShell is not elevated. Open PowerShell with Run as administrator, then repeat with -Execute -RepairAcl. Membership in Administrators without elevation is insufficient.'
    }
}
if ((Git-Output $repoRoot @('config', '--get', 'remote.origin.url')) -ne 'https://github.com/OlegLebedevRU/etranprocessing.git') {
    throw 'Run the script from its original etranprocessing repository.'
}
$pending = @()
$needsAcl = @()
foreach ($target in $targets) {
    if (-not (Test-Path -LiteralPath $target.Path -ErrorAction Stop)) {
        Write-Host "Already absent: $($target.Path)"
        continue
    }
    Assert-TargetPath $target
    Write-Host "Target: $($target.Path)"
    if ($target.Kind -ne 'File') {
        try { Assert-DirectoryTree $target.Path }
        catch [System.UnauthorizedAccessException] {
            if ($Execute -and -not $RepairAcl) { throw 'ACL blocks inspection. Use elevated PowerShell with -Execute -RepairAcl.' }
            $needsAcl += $target.Path
            Write-Warning "ACL blocks inspection: $($target.Path)"
        }
    }
    $pending += $target
}
if (-not $Execute) {
    foreach ($target in $pending) {
        if ($target.Kind -eq 'Clone' -and $needsAcl -notcontains $target.Path) { Assert-PublishedClone $target }
    }
    Write-Host "Preview: $($pending.Count) exact targets; $($needsAcl.Count) require ACL repair. Nothing deleted."
    Write-Host 'Run again with -Execute; add -RepairAcl in elevated PowerShell if permissions block cleanup.'
    return
}

# Complete every validation before the first deletion. ACL repair can occur only
# on explicitly listed temporary targets and is followed by full tree validation.
foreach ($target in $pending) {
    Assert-TargetPath $target
    if ($RepairAcl) { Repair-TargetAcl $target }
    if ($target.Kind -ne 'File') { Assert-DirectoryTree $target.Path }
    if ($target.Kind -eq 'Clone') { Assert-PublishedClone $target }
}
foreach ($target in $pending) {
    Assert-TargetPath $target
    if ($target.Kind -ne 'File') {
        Assert-DirectoryTree $target.Path
        if ($target.Kind -eq 'Clone') { Assert-PublishedClone $target }
        Remove-Item -LiteralPath $target.Path -Recurse -Force -ErrorAction Stop
    } else {
        Remove-Item -LiteralPath $target.Path -Force -ErrorAction Stop
    }
    Write-Host "Removed: $($target.Path)"
}
Write-Host 'Task cleanup completed.'
