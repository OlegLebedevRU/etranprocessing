[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-zA-Z0-9.-]+$')][string]$ProductionHost,
    [Parameter(Mandatory = $true)][string]$ProductionKey,
    [string]$KnownHostsPath = '',
    [switch]$Execute
)
$ErrorActionPreference = 'Stop'
if (-not $Execute) {
    Write-Output 'Preview: verify resources and nginx mount; back up config; disable mutual access log; test; reload; verify. No image build or restart.'
    return
}
if (-not $KnownHostsPath) { $KnownHostsPath = Join-Path $env:USERPROFILE '.ssh/known_hosts' }
foreach ($file in @($ProductionKey, $KnownHostsPath)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required SSH file unavailable: $file" }
}
# Base64 avoids PowerShell 5 native-argument quote stripping. The payload contains
# no credentials and changes only the exact existing access_log directive.
$payload = @'
import json
import os
from pathlib import Path
import re
import subprocess
import time

container = 'nginx-mutual-legacy-nginx-mutual-1'
destination = '/etc/nginx/conf.d/internal_ssl.conf'
expected_source = '/home/user1/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf'

def docker(*args):
    return subprocess.check_output(['docker', *args], text=True)

mem = dict(line.split(':', 1) for line in Path('/proc/meminfo').read_text().splitlines())
disk = os.statvfs('/')
ram = int(mem['MemAvailable'].split()[0]) / 1024
usage = 100 * (disk.f_blocks - disk.f_bfree) / disk.f_blocks
load = os.getloadavg()[0]
print(f'Resources: available RAM={ram:.0f} MiB disk={usage:.1f}% load={load:.2f}', flush=True)
if ram <= 300 or usage >= 90 or load >= 2:
    raise SystemExit('Resource preflight failed')
info = json.loads(docker('inspect', container))[0]
if not info['State']['Running']:
    raise SystemExit('Nginx container is not running')
mounts = [m for m in info['Mounts'] if m['Destination'] == destination]
if len(mounts) != 1 or mounts[0]['Type'] != 'bind' or mounts[0]['Source'] != expected_source:
    raise SystemExit('Unexpected nginx config mount; no change made')
source = Path(expected_source)
if source.is_symlink() or not source.is_file():
    raise SystemExit('Unexpected source file')
original = source.read_bytes()
pattern = rb'(?m)^([ \t]*)access_log[ \t]+/var/log/nginx/ssl-mutual-access\.log[ \t]*;([ \t]*\r?)$'
changed, count = re.subn(pattern, rb'\1access_log off;\2', original)
if count != 1:
    raise SystemExit('Expected exactly one original access_log directive; no change made')
docker('exec', container, 'nginx', '-t')
backup = source.with_name(source.name + '.access-log-backup-' + time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()))
with backup.open('xb') as handle:
    handle.write(original)
    handle.flush()
    os.fsync(handle.fileno())
backup.chmod(0o600)
print(f'Backup: {backup}', flush=True)

def write_config(data):
    # Preserve the inode: nginx reads this file through an individual bind mount.
    with source.open('r+b') as handle:
        handle.write(data)
        handle.truncate()
        handle.flush()
        os.fsync(handle.fileno())

try:
    if source.read_bytes() != original:
        raise SystemExit('Source config changed concurrently; no change made')
    write_config(changed)
    docker('exec', container, 'nginx', '-t')
    docker('exec', container, 'nginx', '-s', 'reload')
    time.sleep(2)
    current = json.loads(docker('inspect', container))[0]
    if current['Id'] != info['Id'] or not current['State']['Running']:
        raise RuntimeError('Nginx container identity/state changed')
    active = docker('exec', container, 'nginx', '-T')
    if re.search(r'^\s*access_log\s+/var/log/nginx/ssl-mutual-access\.log(?:\s|;)', active, re.M):
        raise RuntimeError('Another directive still enables the mutual access log')
    if source.read_bytes() != changed:
        raise RuntimeError('Published config differs from expected change')
except Exception:
    print('Verification failed; restoring backup', flush=True)
    write_config(original)
    docker('exec', container, 'nginx', '-t')
    docker('exec', container, 'nginx', '-s', 'reload')
    raise
print('Config tested and reloaded; same container; mutual access log disabled. Backup retained.', flush=True)
print('Authenticated terminal-route and log-growth checks remain to be performed.', flush=True)
'@
$encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payload.Replace("`r", '')))
$sshOptions = @('-n', '-o', 'BatchMode=yes', '-o', 'IdentitiesOnly=yes', '-o', 'StrictHostKeyChecking=yes',
    '-o', ('UserKnownHostsFile=' + $KnownHostsPath), '-o', 'ConnectTimeout=15')
& ssh @sshOptions -i $ProductionKey ('user1@' + $ProductionHost) "echo $encoded | base64 -d | sudo -n python3"
if ($LASTEXITCODE -ne 0) { throw "Nginx config deployment failed (exit $LASTEXITCODE)." }
