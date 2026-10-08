#requires -Version 5.1
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\FreshTransition.psm1') -Force
$module = Get-Module FreshTransition
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('..\..\dist\.release\fresh-launchers-' + [Guid]::NewGuid().ToString())))
[IO.Directory]::CreateDirectory($root) | Out-Null
$file = Join-Path $root 'leo4proxy.exe'
[IO.File]::WriteAllBytes($file, [byte[]](1,2,3,4))
$hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
& $module { param($Root,$Hash) Assert-L4FreshLaunchers $Root $Hash 4 } $root $hash
[IO.File]::WriteAllBytes($file, [byte[]](1,2,3,5))
$refused = $false
try { & $module { param($Root,$Hash) Assert-L4FreshLaunchers $Root $Hash 4 } $root $hash }
catch { $refused = $true }
if (-not $refused -or [IO.File]::ReadAllBytes($file)[3] -ne 5) { throw 'Mismatch refusal failed or modified existing launcher.' }
[IO.File]::Delete($file)
& $module { param($Root,$Hash) Assert-L4FreshLaunchers $Root $Hash 4 } $root $hash
[IO.File]::WriteAllText((Join-Path $root 'unknown.txt'), 'owned refusal fixture')
$refused = $false
try { & $module { param($Root,$Hash) Assert-L4FreshLaunchers $Root $Hash 4 } $root $hash }
catch { $refused = $true }
if (-not $refused) { throw 'Unknown launcher entry accepted.' }
'PASS: matching launcher/empty directory admitted; mismatch/unknown refused without mutation'
