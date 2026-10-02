[CmdletBinding()]
param([string]$ToolsRoot)
$ErrorActionPreference = 'Stop'
if (-not $ToolsRoot) { $ToolsRoot = (Resolve-Path "$PSScriptRoot\..").Path }
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('l4setup-bootstrap-' + [Guid]::NewGuid())))
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
try {
    foreach ($arch in @('x86','x64')) {
        $dest = Join-Path $fixtureRoot "$arch suite with spaces"
        New-Item -ItemType Directory -Path $dest | Out-Null
        $exe = Join-Path $ToolsRoot "l4superv\bin\$arch\l4superv.exe"
        & $exe --prepare-mosquitto --dest $dest
        if ($LASTEXITCODE -ne 0) { throw "Bootstrap failed: $arch" }
        $conf = Join-Path $dest 'mosquitto\mosquitto.conf'
        $text = [IO.File]::ReadAllText($conf)
        if ($text -notmatch 'listener 1883 127.0.0.1' -or $text -match '(?m)^connection |remote_clientid') {
            throw "Bootstrap is not local-only: $arch"
        }
        [IO.File]::WriteAllText($conf, '# existing configuration must survive')
        & $exe --prepare-mosquitto --dest $dest
        if ($LASTEXITCODE -ne 0 -or [IO.File]::ReadAllText($conf) -ne '# existing configuration must survive') {
            throw "Existing config overwritten: $arch"
        }
        & $exe --prepare-mosquitto --dest relative-path
        if ($LASTEXITCODE -ne 2) { throw "Relative destination accepted: $arch" }
        $invalid = Join-Path $dest 'invalid-destination'
        [IO.File]::WriteAllText($invalid, 'not a directory')
        & $exe --prepare-mosquitto --dest $invalid
        if ($LASTEXITCODE -ne 1) { throw "Unwritable destination falsely succeeded: $arch" }
        if (Test-Path (Join-Path $dest 'state.json')) { throw 'Bootstrap changed orchestrator state.' }
        Write-Host "PASS ${arch}: missing/existing config, spaces, invalid destinations, no state mutation"
    }
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notmatch '^l4setup-bootstrap-[0-9a-f-]+$') { throw 'Unsafe cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
