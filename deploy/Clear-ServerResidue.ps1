[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-zA-Z0-9.-]+$')][string]$ProductionHost,
    [Parameter(Mandatory = $true)][string]$ProductionKey,
    [string]$KnownHostsPath = '',
    [ValidateRange(100, 4096)][int]$JournalMiB = 500,
    [ValidateRange(3, 365)][int]$JournalDays = 7,
    [ValidateRange(24, 8760)][int]$CacheHours = 72,
    [ValidatePattern('^(sha256:)?[a-f0-9]{64}$')][string[]]$ImageId = @(),
    [switch]$Execute
)
$ErrorActionPreference = 'Stop'
if (-not $KnownHostsPath) { $KnownHostsPath = Join-Path $env:USERPROFILE '.ssh/known_hosts' }
foreach ($file in @($ProductionKey, $KnownHostsPath)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required SSH file unavailable: $file" }
}
$settings = @{
    execute = [bool]$Execute
    journal_mib = $JournalMiB
    journal_days = $JournalDays
    cache_hours = $CacheHours
    image_ids = @($ImageId)
} | ConvertTo-Json -Compress
$envData = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($settings))
$payload = @'
import base64
import json
from pathlib import Path
import re
import subprocess
import sys

settings = json.loads(base64.b64decode(sys.argv[1]))

def run(*args):
    return subprocess.check_output(args, text=True)

def docker(*args):
    return run('docker', *args)

print('BEFORE / PREVIEW', flush=True)
print(run('df', '-h', '/'), flush=True)
print(run('journalctl', '--disk-usage'), flush=True)
print(docker('system', 'df'), flush=True)
container_ids = docker('ps', '-aq', '--no-trunc').split()
containers = json.loads(docker('inspect', *container_ids)) if container_ids else []
used = {c['Image'] for c in containers}
running = {c['Name']: c['Id'] for c in containers if c['State']['Running']}
release_refs = set()
# Only release metadata is read; no .env or Docker credential files.
def collect(value):
    if isinstance(value, str):
        release_refs.update(re.findall(r'sha256:[a-f0-9]{64}', value))
    elif isinstance(value, dict):
        for child in value.values(): collect(child)
    elif isinstance(value, list):
        for child in value: collect(child)
for file in Path('/home/user1/.etran-ci').glob('*release.json'):
    collect(json.loads(file.read_text()))
targets = []
for identifier in settings['image_ids']:
    image = json.loads(docker('image', 'inspect', identifier))[0]
    tags = image.get('RepoTags') or []
    digests = image.get('RepoDigests') or []
    if image['Id'] in used:
        raise SystemExit('Refusing image used by a running or stopped container: ' + identifier)
    if any(re.search(r'rollback|baseline|(?:^|[-:])pre(?:[-:]|$)', tag, re.I) for tag in tags):
        raise SystemExit('Refusing rollback/baseline image: ' + identifier)
    if image['Id'] in release_refs or any(d.split('@')[-1] in release_refs for d in digests):
        raise SystemExit('Refusing image referenced by release metadata: ' + identifier)
    targets.append(image['Id'])
    print('Reviewed image target:', image['Id'], tags, flush=True)
print(f"Plan: rotate journal, then limit archived journals to {settings['journal_mib']} MiB and {settings['journal_days']} days; active journals are excluded. Prune unused build cache older than {settings['cache_hours']} hours.", flush=True)
print('No container, volume, application file or image is automatically removed.', flush=True)
print('Explicit image targets:', len(set(targets)), flush=True)
if not settings['execute']:
    print('PREVIEW ONLY. Run with -Execute to clean.', flush=True)
    raise SystemExit(0)
run('journalctl', '--rotate')
print(run('journalctl', f"--vacuum-size={settings['journal_mib']}M", f"--vacuum-time={settings['journal_days']}d"), flush=True)
print(docker('builder', 'prune', '--force', '--filter', f"until={settings['cache_hours']}h"), flush=True)
for identifier in dict.fromkeys(targets):
    # Docker performs a second in-use check; never force removal.
    print(docker('image', 'rm', identifier), flush=True)
after_ids = docker('ps', '-q', '--no-trunc').split()
after = json.loads(docker('inspect', *after_ids)) if after_ids else []
if {c['Name']: c['Id'] for c in after} != running:
    raise SystemExit('Running container set changed during cleanup; investigate')
print('AFTER', flush=True)
print(run('df', '-h', '/'), flush=True)
print(run('journalctl', '--disk-usage'), flush=True)
print(docker('system', 'df'), flush=True)
print('Cleanup completed; running containers unchanged.', flush=True)
'@
$encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payload.Replace("`r", '')))
$sshOptions = @('-n', '-o', 'BatchMode=yes', '-o', 'IdentitiesOnly=yes', '-o', 'StrictHostKeyChecking=yes',
    '-o', ('UserKnownHostsFile=' + $KnownHostsPath), '-o', 'ConnectTimeout=15')
& ssh @sshOptions -i $ProductionKey ('user1@' + $ProductionHost) "echo $encoded | base64 -d | sudo -n python3 - $envData"
if ($LASTEXITCODE -ne 0) { throw "Server cleanup failed (exit $LASTEXITCODE)." }
