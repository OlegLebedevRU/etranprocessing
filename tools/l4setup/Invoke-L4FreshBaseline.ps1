#requires -Version 5.1
# Explicit operator cold replacement; no configuration migration or RPC entry.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Bundle,
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][string]$InstallerSha256,
    [Parameter(Mandatory=$true)][string]$SourceBundle,
    [Parameter(Mandatory=$true)][string]$SourceVersion,
    [Parameter(Mandatory=$true)][string]$SourceInstallerSha256,
    [Parameter(Mandatory=$true)][string]$OriginOperation,
    [Parameter(Mandatory=$true)][string]$OriginVersion,
    [Parameter(Mandatory=$true)][string]$OriginInstallerSha256,
    [ValidateSet('x86','x64')][string]$Arch='x86', [switch]$Apply)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'FreshTransition.psm1') -Force
try {
    if ($Apply -and (-not (Test-L4Administrator) -or $PSVersionTable.PSEdition -ne 'Desktop' -or ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess))) {
        function Literal([string]$Value) { return "'"+$Value.Replace("'","''")+"'" }
        $arguments = @()
        foreach ($key in @('Bundle','Version','InstallerSha256','SourceBundle','SourceVersion','SourceInstallerSha256','OriginOperation','OriginVersion','OriginInstallerSha256','Arch')) {
            $arguments += '-'+$key+' '+(Literal ([string](Get-Variable -Name $key -ValueOnly)))
        }
        $command = '& '+(Literal $PSCommandPath)+' '+($arguments -join ' ')+' -Apply; exit $LASTEXITCODE'
        $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
        $exe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) { $exe=Join-Path $env:SystemRoot 'Sysnative\WindowsPowerShell\v1.0\powershell.exe' }
        $launch=@{FilePath=$exe;WindowStyle='Hidden';ArgumentList=@('-NoProfile','-NonInteractive','-EncodedCommand',$encoded);Wait=$true;PassThru=$true}
        if (-not (Test-L4Administrator)) { $launch.Verb='RunAs' }
        $child=Start-Process @launch; $code=$child.ExitCode; $child.Dispose(); exit $code
    }
    $plan=Get-L4FreshBaselinePlan -Bundle $Bundle -Version $Version -InstallerSha256 $InstallerSha256 -SourceBundle $SourceBundle -SourceVersion $SourceVersion -SourceInstallerSha256 $SourceInstallerSha256 -OriginOperation $OriginOperation -OriginVersion $OriginVersion -OriginInstallerSha256 $OriginInstallerSha256 -Arch $Arch
    if (-not $Apply) {
        $plan | Select-Object Version,SourceVersion,OriginOperation,OriginVersion,Arch,RetireOrder,ConnectionOutage | ConvertTo-Json
        exit 0
    }
    Invoke-L4FreshTransition -Plan $plan
    exit 0
} catch { Write-Error -ErrorRecord $_ -ErrorAction Continue; exit 1 }
