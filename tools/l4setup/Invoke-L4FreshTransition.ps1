#requires -Version 5.1
<#!
Operator-controlled retirement, then the existing signed fresh installer.
Default is a read-only plan. -Apply requests UAC; this is not migration or RPC.
The SHA256 is supplied from the independently reviewed readiness handoff.
!#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Bundle,
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$InstallerSha256,
    [ValidateSet('x86', 'x64')][string]$Arch = 'x86',
    [switch]$Apply
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'FreshTransition.psm1') -Force

try {
    if ($Apply -and (-not (Test-L4Administrator) -or $PSVersionTable.PSEdition -ne 'Desktop' -or ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess))) {
        # Construct literal arguments, never interpolate operator input as code.
        function Literal([string]$Value) { return "'" + $Value.Replace("'", "''") + "'" }
        $scriptFile = Literal $PSCommandPath
        $bundlePath = Literal ([IO.Path]::GetFullPath($Bundle))
        $script = "`$code = 1; try { & $scriptFile -Bundle $bundlePath -Version $(Literal $Version) -InstallerSha256 $(Literal $InstallerSha256) -Arch $(Literal $Arch) -Apply; `$code = `$LASTEXITCODE } catch { Write-Error `$_ }; Read-Host 'Press Enter to close'; exit `$code"
        $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($script))
        $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) {
            $powershell = Join-Path $env:SystemRoot 'Sysnative\WindowsPowerShell\v1.0\powershell.exe'
        }
        # Visible window is intentional: this is an interactive operator procedure.
        $launch = @{ FilePath = $powershell; WindowStyle = 'Normal'; ArgumentList = @('-NoProfile', '-EncodedCommand', $encoded); Wait = $true; PassThru = $true }
        if (-not (Test-L4Administrator)) { $launch.Verb = 'RunAs' }
        $child = Start-Process @launch
        $code = $child.ExitCode
        $child.Dispose()
        exit $code
    }
    $plan = Get-L4FreshTransitionPlan -Bundle $Bundle -Version $Version -InstallerSha256 $InstallerSha256 -Arch $Arch
    if (-not $Apply) {
        $plan | Select-Object Bundle, Version, Arch, InstallerSha256, RetireOrder, OldPathEntries, ConnectionOutage | ConvertTo-Json -Depth 5
        Write-Host 'Read-only plan. Run the same command with -Apply for UAC and installation.'
        exit 0
    }
    Invoke-L4FreshTransition -Plan $plan
    exit 0
} catch {
    Write-Error -ErrorRecord $_ -ErrorAction Continue
    exit 1
}
