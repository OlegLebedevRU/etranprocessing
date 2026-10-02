[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-zA-Z0-9.-]+$')][string]$BuilderHost,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-zA-Z0-9.-]+$')][string]$ProductionHost,
    [Parameter(Mandatory = $true)][string]$BuilderKey,
    [Parameter(Mandatory = $true)][string]$ProductionKey,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-f0-9]{40}$')][string]$Revision,
    [string]$KnownHostsPath = '',
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Frontend images are delivery artifacts: the standard deployer publishes /dist
# into the existing nginx mount and verifies HTTP without restarting nginx.
if (-not $Execute) {
    Write-Output "Preview: main $Revision -> builder $BuilderHost -> registry -> production $ProductionHost"
    Write-Output 'Order: menubuilder-backend, then menubuilder-frontend (static dist).'
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
    return $quote + $Value.Replace($quote, $quote + '"' + $quote + '"' + $quote) + $quote
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
