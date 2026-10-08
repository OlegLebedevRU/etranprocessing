#requires -Version 5.1
# No SCM/PATH mutation, UAC, installer launch, or legacy directory deletion.
param([string]$Bundle = '', [string]$InstallerSha256 = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot '..\FreshTransition.psm1'
Import-Module $modulePath -Force
$module = Get-Module FreshTransition
$controller = [System.ServiceProcess.ServiceController]::new('L4Con')
try {
    # Actual query-only API in a clean -NoProfile process; no Stop/Start/Delete.
    $null = $controller.Status
    foreach ($dependent in $controller.DependentServices) { $dependent.Dispose() }
    Write-Host 'Actual ServiceController assembly + query-only SCM PASS'
} finally { $controller.Dispose() }
& $module {
    $script:Passed = 0
    function Check([bool]$Ok, [string]$Label) { if (-not $Ok) { throw "FAIL: $Label" }; $script:Passed++ }
    function Refuses([scriptblock]$Action, [string]$Label) {
        $failed = $false
        try { & $Action | Out-Null } catch { $failed = $true }
        Check $failed $Label
    }
    $path = ';%SystemRoot%\System32; "c:\L4TOOLS\l4con\" ;C:\unrelated;;C:\l4tools;C:\l4tools-other;'
    $split = Split-L4OldPath $path
    Check ($split.Removed.Count -eq 2) 'exact old entries only'
    Check ($split.Original -ceq $path) 'original raw value preserved'
    Check ($split.Value -ceq ';%SystemRoot%\System32;C:\unrelated;;C:\l4tools-other;') 'unrelated/empty/placeholders remain exact'
    Check ((Split-L4OldPath 'C:\l4tools;C:\l4tools').Removed.Count -eq 2) 'duplicate old entries removed'
    Check ((Split-L4OldPath 'C:\unrelated').Value -ceq 'C:\unrelated') 'no old entries is unchanged'
    Refuses { Split-L4OldPath 'C:\l4tools\unreviewed' } 'unknown old descendant'
    Refuses { Split-L4OldPath '%SystemDrive%\l4tools' } 'unreviewed expanded old path'
    $s = [pscustomobject]@{ Name = 'L4Con'; PathName = '"C:\l4tools\l4con\l4con.exe" --service'; StartName = 'LocalSystem'; StartMode = 'Auto'; ServiceType = 'Own Process'; Image = 'C:\l4tools\l4con\l4con.exe'; Sha256 = 'abc'; State = 'Running'; ProcessId = 123; CreationUtc = 111 }
    Assert-L4ServiceSnapshot $s
    Check $true 'original canonical service admitted'
    foreach ($change in @(@('PathName', '"C:\foreign\l4con.exe" --service'), @('PathName', 'C:\l4tools\l4con\l4con.exe --service'), @('Name', 'other'), @('StartName', 'user'), @('StartMode', 'Manual'), @('ServiceType', 'Share Process'))) {
        $copy = $s.PSObject.Copy()
        $copy.($change[0]) = $change[1]
        Refuses { Assert-L4ServiceSnapshot $copy } "foreign $($change[0])"
    }
    Assert-L4SameService $s $s
    Check $true 'same original fingerprint'
    foreach ($field in @('PathName', 'StartName', 'StartMode', 'ServiceType', 'Image', 'Sha256', 'Name', 'ProcessId', 'State', 'CreationUtc')) {
        $copy = $s.PSObject.Copy()
        if ($field -eq 'ProcessId') { $copy.ProcessId = 124 } else { $copy.$field = 'changed' }
        Refuses { Assert-L4SameService $s $copy } "drift $field"
    }
    $stopped = $s.PSObject.Copy(); $stopped.State = 'Stopped'; $stopped.ProcessId = 0
    Assert-L4SameService $s $stopped -Stopped
    Check $true 'stopped original accepted'
    Refuses { Assert-L4SameService $s $s -Stopped } 'running not deletion ready'
    $stopped.ProcessId = 123
    Refuses { Assert-L4SameService $s $stopped -Stopped } 'stopped with nonzero PID refused'
    foreach ($bundle in @('relative', '\\server\share', 'C:\kit:stream', 'C:\bad"kit')) {
        Refuses { Get-L4FreshTransitionPlan -Bundle $bundle -Version '1.13.5' -InstallerSha256 ('a' * 64) } 'invalid kit location'
    }
    Write-Host "$script:Passed transition guard checks passed; no live mutations."
}
if ($Bundle) {
    & $module {
        param($Bundle, $Pin)
        $plan = Get-L4FreshTransitionPlan -Bundle $Bundle -Version '1.13.5' -InstallerSha256 $Pin
        # Exercise the real file holds and orchestration up to a failing verifier.
        # Mock only native execution; no actual SYSTEM host or suite changes.
        $script:VerifyCalls = 0
        $script:MutationCalls = 0
        function Invoke-L4Native([string]$File, [string[]]$Arguments) {
            $script:VerifyCalls++
            if ($Arguments[0] -ne '--fresh-verify') { throw 'Unexpected native call in refusal test.' }
            throw 'Fixture verifier refusal.'
        }
        function New-L4PrivateRecord { $script:MutationCalls++; throw 'Mutation must not be reached.' }
        function Remove-L4RetiredDirectory { $script:MutationCalls++; throw 'Deletion must not be reached.' }
        function Test-L4Administrator { return $true }
        $failed = $false
        try { Invoke-L4FreshTransition -Plan $plan } catch { $failed = $_.Exception.Message -eq 'Fixture verifier refusal.' }
        if (-not $failed -or $script:VerifyCalls -ne 1 -or $script:MutationCalls -ne 0) { throw 'Verifier refusal did not stop before mutations.' }
        Write-Host 'Actual kit holds + modeled verifier failure before retirement PASS'
    } $Bundle $InstallerSha256
}
# Parse the entry using the actual Windows PowerShell 5.1 parser.
$tokens = $null; $errors = $null
$entry = Join-Path $PSScriptRoot '..\Invoke-L4FreshTransition.ps1'
[Management.Automation.Language.Parser]::ParseFile($entry, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count) { throw ($errors | Out-String) }
Write-Host 'Windows PowerShell entry syntax PASS'
