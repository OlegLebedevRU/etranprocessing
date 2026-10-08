#requires -Version 5.1
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\FreshTransition.psm1') -Force
$module = Get-Module FreshTransition
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class L4DiagnosticMoveFixture {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr CreateFileW(string p,uint access,uint share,IntPtr security,uint mode,uint flags,IntPtr template);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
'@
$fixture = Join-Path $PSScriptRoot ('..\..\dist\.release\diagnostic-move-' + [Guid]::NewGuid().ToString())
$fixture = [IO.Path]::GetFullPath($fixture)
$sourceRoot = Join-Path $fixture 'config'
$operation = Join-Path $fixture 'update\operations\fixture'
[IO.Directory]::CreateDirectory((Join-Path $sourceRoot 'mosquitto')) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $operation 'failed-runtime')) | Out-Null
$source = Join-Path $sourceRoot 'mosquitto\fixture.log'
[IO.File]::WriteAllText($source, 'owned fixture bytes')
$before = (Get-Acl -LiteralPath $source).Sddl
# Keep a directory reader open while exercising the real file-move helper.
# This fixture validates file retention, not the cause of the live tree failure.
$hold = [L4DiagnosticMoveFixture]::CreateFileW($sourceRoot,0x80,3,[IntPtr]::Zero,3,0x02200000,[IntPtr]::Zero)
if ($hold -eq [IntPtr]::new(-1)) { throw 'Cannot establish directory reader fixture.' }
try {
    & $module { param($Data,$Operation,$Source) Move-L4DiagnosticFile $Data $Operation $Source } $fixture $operation $source
    $target = Join-Path $operation 'failed-runtime\config\mosquitto\fixture.log'
    if ([IO.File]::ReadAllText($target) -cne 'owned fixture bytes' -or (Get-Acl -LiteralPath $target).Sddl -cne $before -or -not (Test-Path -LiteralPath $sourceRoot)) { throw 'File move changed bytes, ACL or source directory.' }
    $outsideRefused = $false
    try { & $module { param($Data,$Operation,$Source) Move-L4DiagnosticFile $Data $Operation $Source } $fixture $operation $target }
    catch { $outsideRefused = $true }
    if (-not $outsideRefused) { throw 'Operation area accepted as runtime source.' }
    'PASS: held directory, file bytes/ACL/root preserved, non-runtime source refused'
} finally { [L4DiagnosticMoveFixture]::CloseHandle($hold) | Out-Null }
# Keep the owned ignored fixture for evidence; no live stand file was modified.
