#requires -Version 5.1
# Real isolated marker/lock checks; no live SCM/configuration mutation.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '..\FreshTransition.psm1') -Force
$module=Get-Module FreshTransition
$root=Join-Path ([IO.Path]::GetTempPath()) ('L4BaselineGuard-'+[Guid]::NewGuid().ToString())
$operations=Join-Path $root 'update\operations'
[IO.Directory]::CreateDirectory($operations) | Out-Null
try {
    & $module {
        param($Root,$Operations)
        $script:Checks=0
        function Check([bool]$Ok) { if(-not $Ok) { throw 'Baseline guard failed.' }; $script:Checks++ }
        function Refuses([scriptblock]$Action) { $refused=$false; try { & $Action } catch { $refused=$true }; Check $refused }
        function Marker([string]$Owner='', [uint32]$Window=0, [uint64]$Generation=0, [uint64]$Plan=0, [uint64]$Deadline=0) {
            $b=[byte[]]::new(112)
            [Text.Encoding]::ASCII.GetBytes("L4UPD01`0").CopyTo($b,0)
            [Text.Encoding]::ASCII.GetBytes($Owner).CopyTo($b,8)
            [BitConverter]::GetBytes($Window).CopyTo($b,48)
            [BitConverter]::GetBytes($Generation).CopyTo($b,56)
            [BitConverter]::GetBytes($Plan).CopyTo($b,64)
            [BitConverter]::GetBytes($Deadline).CopyTo($b,72)
            $sha=[Security.Cryptography.SHA256]::Create()
            try { $sha.ComputeHash($b,0,80).CopyTo($b,80) } finally { $sha.Dispose() }
            [IO.File]::WriteAllBytes((Join-Path $Operations 'update.state'),$b)
        }
        Marker; Assert-L4BaselineIdle $Root; Check $true
        $owner='135a4120-9ba6-4f6c-8cac-4baf5df8f1df'
        Marker $owner 0 3 44 0; Assert-L4BaselineIdle $Root; Check $true
        Marker $owner 1 3 44 999; Refuses { Assert-L4BaselineIdle $Root }
        Marker $owner 2 3 44 999; Refuses { Assert-L4BaselineIdle $Root }
        Marker $owner 0 3 44 999; Refuses { Assert-L4BaselineIdle $Root }
        Marker $owner 0 0 0 0; Refuses { Assert-L4BaselineIdle $Root }
        Marker ('a'*36) 0 3 44 0; Refuses { Assert-L4BaselineIdle $Root }
        Marker '00000000-0000-0000-0000-000000000000' 0 3 44 0; Refuses { Assert-L4BaselineIdle $Root }
        Marker $owner 0 3 0 0; Refuses { Assert-L4BaselineIdle $Root }
        Marker; $path=Join-Path $Operations 'update.state'; $bytes=[IO.File]::ReadAllBytes($path); $bytes[80]=$bytes[80] -bxor 1; [IO.File]::WriteAllBytes($path,$bytes); Refuses { Assert-L4BaselineIdle $Root }
        [IO.File]::WriteAllBytes($path,[byte[]]::new(80)); Refuses { Assert-L4BaselineIdle $Root }
        $lockPath=Join-Path $Operations 'deployment.lock'; [IO.File]::WriteAllBytes($lockPath,[byte[]]::new(0))
        $reservation=[IO.File]::Open($lockPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        try { Refuses { $writer=[IO.File]::Open($lockPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None); $writer.Dispose() } } finally { $reservation.Dispose() }
        $writer=[IO.File]::Open($lockPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None); $writer.Dispose(); Check $true
        Write-Host "$script:Checks cold baseline marker/lock checks PASS; live services unchanged."
    } $root $operations
} finally {
    # Task-created exact files/directories only, no recursive delete.
    $absolute=[IO.Path]::GetFullPath($root)
    if(-not $absolute.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture cleanup escaped TEMP.' }
    foreach($leaf in @('update.state','deployment.lock')) { $file=Join-Path $operations $leaf; if(Test-Path -LiteralPath $file) { [IO.File]::Delete($file) } }
    [IO.Directory]::Delete($operations,$false)
    [IO.Directory]::Delete((Join-Path $root 'update'),$false)
    [IO.Directory]::Delete($root,$false)
}
