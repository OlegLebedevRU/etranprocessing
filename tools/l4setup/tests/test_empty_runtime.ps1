#requires -Version 5.1
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\FreshTransition.psm1') -Force
$module = Get-Module FreshTransition
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('..\..\dist\.release\empty-runtime-' + [Guid]::NewGuid().ToString())))
foreach ($area in @('config', 'state', 'logs')) { [IO.Directory]::CreateDirectory((Join-Path $root "$area\nested\leaf")) | Out-Null }
$file = Join-Path $root 'logs\nested\leaf\guard.txt'
[IO.File]::WriteAllText($file, 'retained fixture')
$refused = $false
try { & $module { param($Root) Remove-L4EmptyRuntimeDirectories $Root } $root }
catch { $refused = $true }
if (-not $refused -or -not (Test-Path (Join-Path $root 'config\nested\leaf')) -or [IO.File]::ReadAllText($file) -cne 'retained fixture') { throw 'File guard did not refuse before deletion.' }
[IO.File]::Delete($file)
& $module { param($Root) Remove-L4EmptyRuntimeDirectories $Root } $root
foreach ($area in @('config', 'state', 'logs')) {
    $path = Join-Path $root $area
    if (-not (Test-Path -LiteralPath $path) -or @(Get-ChildItem -LiteralPath $path -Force).Count) { throw 'Native empty-root invariant failed.' }
}
$archive = Join-Path $root 'update\operations\fixture\failed-runtime\state'
[IO.Directory]::CreateDirectory($archive) | Out-Null
[IO.File]::WriteAllText((Join-Path $archive 'retained.log'), 'private diagnostic fixture')
& $module { param($Root) Remove-L4EmptyRuntimeDirectories $Root } $root
if (-not (Test-Path (Join-Path $archive 'retained.log'))) { throw 'Diagnostic archive changed.' }
& $module { param($Root) Assert-L4NativeFreshData $Root } $root
$marker = Join-Path $root 'update\operations\update.state'
[IO.File]::WriteAllText($marker, 'fixture marker: must refuse regardless of content')
$refused = $false
try { & $module { param($Root) Assert-L4NativeFreshData $Root } $root }
catch { $refused = $true }
if (-not $refused -or -not (Test-Path $marker)) { throw 'Fresh marker refusal failed or modified marker.' }
[IO.File]::Delete($marker)
$emptyLeaf = Join-Path $root 'config\empty'
[IO.Directory]::CreateDirectory($emptyLeaf) | Out-Null
$refused = $false
try { & $module { param($Root) Assert-L4NativeFreshData $Root } $root }
catch { $refused = $true }
if (-not $refused) { throw 'Native admission accepted an empty child directory.' }
[IO.Directory]::Delete($emptyLeaf, $false)
& $module { param($Root) Assert-L4NativeFreshData $Root } $root
'PASS: file/empty-directory/marker refusal, empty-only removal, roots/archive retained, repeat safe'
