[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-zA-Z0-9.-]+$')][string]$BuilderHost,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-zA-Z0-9.-]+$')][string]$ProductionHost,
    [Parameter(Mandatory = $true)][string]$BuilderKey,
    [Parameter(Mandatory = $true)][string]$ProductionKey,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-f0-9]{40}$')][string]$Revision,
    [string]$KnownHostsPath = '',
    [string]$SourcePath = '',
    [switch]$Publish,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Frontend images are delivery artifacts: the standard deployer publishes /dist
# into the existing nginx mount and verifies HTTP without restarting nginx.
if (-not $Execute) {
    Write-Output "Preview: main $Revision -> builder $BuilderHost -> registry -> production $ProductionHost"
    Write-Output 'Order: menubuilder-backend, then menubuilder-frontend (static dist).'
    if ($Publish) { Write-Output 'Publish the specified clean local commit before deployment.' }
    Write-Output 'Run with -Execute to build and deploy.'
    return
}

if (-not $KnownHostsPath) { $KnownHostsPath = Join-Path $env:USERPROFILE '.ssh/known_hosts' }
foreach ($file in @($BuilderKey, $ProductionKey, $KnownHostsPath)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required SSH file is unavailable: $file. Supply readable keys and a previously trusted known_hosts."
    }
}
foreach ($command in @('ssh', 'git')) { Get-Command $command -ErrorAction Stop | Out-Null }
$KnownHostsPath = (Resolve-Path -LiteralPath $KnownHostsPath).Path
$sshOptions = @('-n', '-o', 'BatchMode=yes', '-o', 'IdentitiesOnly=yes', '-o', 'StrictHostKeyChecking=yes',
    '-o', ('UserKnownHostsFile=' + $KnownHostsPath), '-o', 'ConnectTimeout=15')

function Shell-Quote([string]$Value) {
    $quote = "'"
    # POSIX single-quote escaping without double quotes: Windows PowerShell 5
    # strips embedded double quotes when marshalling native ssh arguments.
    return $quote + $Value.Replace($quote, $quote + '\' + $quote + $quote) + $quote
}

function Read-Ssh([string]$Server, [string]$Key, [string]$Command) {
    $output = & ssh @sshOptions -i $Key ('user1@' + $Server) $Command
    if ($LASTEXITCODE -ne 0) { throw "SSH check failed on $Server (exit $LASTEXITCODE)." }
    return (@($output) -join "`n").Trim()
}

function Assert-Main {
    $output = & git ls-remote 'https://github.com/OlegLebedevRU/etranprocessing.git' refs/heads/main
    if ($LASTEXITCODE -ne 0 -or -not $output) { throw 'Cannot verify published main.' }
    if (([string]$output -split '\s+')[0] -ne $Revision) {
        throw 'main changed. Review the new revision before deploying; do not simply replace Revision.'
    }
}

function Publish-Revision {
    if (-not $SourcePath) {
        $repositoryRoot = Split-Path $PSScriptRoot -Parent
        $releaseCheckout = Join-Path $repositoryRoot '.ui-release-checkout'
        $SourcePath = if (Test-Path -LiteralPath $releaseCheckout -PathType Container) { $releaseCheckout } else { $repositoryRoot }
    }
    $source = (Resolve-Path -LiteralPath $SourcePath).Path
    # The reviewed checkout belongs to the sandbox account. Trust this exact
    # directory only for these commands when run from the operator's account.
    $sourceGitOptions = @('-c', ('safe.directory=' + $source.Replace('\', '/')), '-C', $source)
    $head = & git @sourceGitOptions rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the local source checkout.' }
    if ($head -ne $Revision) { throw 'Local source HEAD does not match Revision.' }
    $state = & git @sourceGitOptions status --porcelain
    if ($LASTEXITCODE -ne 0 -or $state) { throw 'Source checkout must be clean before publication.' }
    $remote = & git @sourceGitOptions remote get-url origin
    if ($LASTEXITCODE -ne 0 -or $remote -ne 'https://github.com/OlegLebedevRU/etranprocessing.git') {
        throw 'Unexpected source remote.'
    }
    $published = & git ls-remote $remote refs/heads/main
    if ($LASTEXITCODE -ne 0 -or -not $published) { throw 'Cannot verify publication baseline.' }
    $publishedSha = ([string]$published -split '\s+')[0]
    if ($publishedSha -eq $Revision) { return }
    $parent = & git @sourceGitOptions rev-parse 'HEAD^'
    if ($LASTEXITCODE -ne 0 -or $publishedSha -ne $parent) {
        throw 'main changed or local commit has an unexpected parent. Review before publication.'
    }
    Get-Command gh -ErrorAction Stop | Out-Null
    # Bypass the failing credential-helper subprocess using an ephemeral HTTP
    # header for normal git push. Never store the token in a file or Git config.
    $previousGitCount = [Environment]::GetEnvironmentVariable('GIT_CONFIG_COUNT')
    if ($previousGitCount -and $previousGitCount -notmatch '^\d+$') { throw 'Invalid process Git override count.' }
    $configIndex = if ($previousGitCount) { [int]$previousGitCount } else { 0 }
    $headerKey = 'GIT_CONFIG_KEY_' + $configIndex
    $headerValue = 'GIT_CONFIG_VALUE_' + $configIndex
    if ([Environment]::GetEnvironmentVariable($headerKey) -or [Environment]::GetEnvironmentVariable($headerValue)) {
        throw 'Unexpected process Git override at the new header index.'
    }
    $taskToken = & gh auth token --hostname github.com
    if ($LASTEXITCODE -ne 0 -or -not $taskToken) { throw 'GitHub login is unavailable in this Windows profile.' }
    try {
        $env:GIT_CONFIG_COUNT = [string]($configIndex + 1)
        [Environment]::SetEnvironmentVariable($headerKey, 'http.https://github.com/.extraheader')
        [Environment]::SetEnvironmentVariable($headerValue, 'Authorization: Basic ' + [Convert]::ToBase64String(
            [Text.Encoding]::UTF8.GetBytes('OlegLebedevRU:' + $taskToken)))
        & git @sourceGitOptions push origin ($Revision + ':refs/heads/main')
        if ($LASTEXITCODE -ne 0) { throw 'git push failed; deployment was not started.' }
    } finally {
        [Environment]::SetEnvironmentVariable($headerKey, $null)
        [Environment]::SetEnvironmentVariable($headerValue, $null)
        [Environment]::SetEnvironmentVariable('GIT_CONFIG_COUNT', $previousGitCount)
        $taskToken = $null
    }
    Assert-Main
    Write-Output "Published and verified: $Revision"
}

if ($Publish) { Publish-Revision }

$resourceProbe = @'
import json, os
from pathlib import Path
mem = dict(line.split(':', 1) for line in Path('/proc/meminfo').read_text().splitlines())
disk = os.statvfs('/')
print(json.dumps({'ram_mib': int(mem['MemAvailable'].split()[0]) / 1024,
                  'disk_percent': 100 * (disk.f_blocks - disk.f_bfree) / disk.f_blocks,
                  'disk_free_gib': disk.f_bavail * disk.f_frsize / 1024**3,
                  'load': os.getloadavg()[0]}))
'@
Assert-Main
foreach ($server in @($BuilderHost, $ProductionHost)) {
    $key = if ($server -eq $BuilderHost) { $BuilderKey } else { $ProductionKey }
    $resources = Read-Ssh $server $key ('python3 -c ' + (Shell-Quote $resourceProbe)) | ConvertFrom-Json
    $minimumRam = if ($server -eq $BuilderHost) { 800 } else { 300 }
    if ($resources.ram_mib -le $minimumRam -or $resources.disk_percent -ge 90 -or
        $resources.load -ge 2 -or ($server -eq $BuilderHost -and $resources.disk_free_gib -lt 2)) {
        throw "Resource preflight failed on ${server}: $($resources | ConvertTo-Json -Compress)"
    }
    Write-Output "Resource preflight passed: $server"
}

$containerCommand = "sudo -n docker ps --no-trunc --format '{{.Names}} {{.ID}}'"
$before = Read-Ssh $ProductionHost $ProductionKey $containerCommand
$frontendMounts = Read-Ssh $ProductionHost $ProductionKey "sudo -n docker inspect nginx-default --format '{{json .Mounts}}'" | ConvertFrom-Json
if (-not @($frontendMounts | Where-Object {
    $_.Source -eq '/home/user1/MenuBuilder/frontend/dist' -and $_.Destination -eq '/usr/share/nginx/html'
}).Count) { throw 'Unexpected frontend mount. Refusing deployment.' }

foreach ($component in @('menubuilder-backend', 'menubuilder-frontend')) {
    Assert-Main
    Write-Output "Building and deploying $component from $Revision ..."
    & ssh @sshOptions -i $BuilderKey ('user1@' + $BuilderHost) (
        'sudo -n -u github-runner -H python3 /opt/etran-beta/launcher.py --component ' + $component)
    if ($LASTEXITCODE -ne 0) { throw "$component release failed (exit $LASTEXITCODE)." }
    Assert-Main
    $record = Read-Ssh $ProductionHost $ProductionKey (
        'cat /home/user1/.etran-ci/' + $component + '-release.json') | ConvertFrom-Json
    if ($record.revision -ne $Revision -or $record.image -notmatch '@sha256:[a-f0-9]{64}$') {
        throw "Unexpected $component production release record."
    }
    if ($component -eq 'menubuilder-backend') {
        $inspection = Read-Ssh $ProductionHost $ProductionKey 'sudo -n docker inspect menubuilder-backend' | ConvertFrom-Json
        $container = @($inspection)[0]
        $imageId = Read-Ssh $ProductionHost $ProductionKey (
            'sudo -n docker image inspect --format ' + (Shell-Quote '{{.Id}}') + ' ' + (Shell-Quote $record.image))
        if ($container.Config.Labels.'org.opencontainers.image.revision' -ne $Revision -or
            -not $container.State.Running -or $container.Image -ne $imageId) {
            throw 'Backend runtime revision, state or image ID mismatch.'
        }
    }
    Write-Output "Verified $component : $($record.image)"
}

$after = Read-Ssh $ProductionHost $ProductionKey $containerCommand
$neighborsBefore = @($before -split "`n" | Where-Object { $_ -notmatch '^menubuilder-backend ' } | Sort-Object)
$neighborsAfter = @($after -split "`n" | Where-Object { $_ -notmatch '^menubuilder-backend ' } | Sort-Object)
if (($neighborsBefore -join "`n") -ne ($neighborsAfter -join "`n")) {
    throw 'Neighboring container IDs changed during release. Investigate before claiming success.'
}
Write-Output "Deployment verified: $Revision. Backend health and frontend HTTP were checked by the standard deployer."
Write-Output 'Verify the role 5 monitoring denial, terminal filters and video selection in the browser.'
