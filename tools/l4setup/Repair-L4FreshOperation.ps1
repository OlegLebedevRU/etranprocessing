#requires -Version 5.1
<#! Explicit operator repair of an inspected uncommitted fresh operation.
No epoch adoption, journal editing, force-kill or legacy restoration.
The native abort remains mandatory before the next clean installation.
!#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$')][string]$PreviousOperation,
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$PreviousVersion,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$PreviousInstallerSha256,
    [Parameter(Mandatory = $true)][string]$Bundle,
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$InstallerSha256,
    [ValidateSet('x86', 'x64')][string]$Arch = 'x86',
    [switch]$Apply,
    [switch]$ResumeAbort,
    [switch]$ResumeAfterAbort,
    [ValidatePattern('^[a-fA-F0-9]{64}$')][string]$DiagnosticInventorySha256,
    [ValidatePattern('^[a-fA-F0-9]{64}$')][string]$BrokerDirectoryPolicySha256
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'FreshTransition.psm1') -Force
$module = Get-Module FreshTransition
if (-not ('L4FreshRepairSecurity' -as [type])) {
    Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class L4FreshRepairSecurity {
    [DllImport("advapi32.dll", EntryPoint="GetFileSecurityW", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
    private static extern bool GetSecurity(string path,uint flags,byte[] bytes,uint size,out uint needed);
    [DllImport("advapi32.dll", EntryPoint="SetFileSecurityW", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
    private static extern bool SetSecurity(string path,uint flags,byte[] bytes);
    public static byte[] Read(string path) {
        uint needed;
        if (GetSecurity(path,7,null,0,out needed) || Marshal.GetLastWin32Error()!=122 || needed==0 || needed>4096)
            throw new Win32Exception(Marshal.GetLastWin32Error());
        byte[] bytes=new byte[needed];
        if (!GetSecurity(path,7,bytes,needed,out needed)) throw new Win32Exception(Marshal.GetLastWin32Error());
        return bytes;
    }
    public static void Restore(string path,byte[] bytes) {
        if (!SetSecurity(path,7,bytes)) throw new Win32Exception(Marshal.GetLastWin32Error());
        if (Convert.ToBase64String(Read(path))!=Convert.ToBase64String(bytes))
            throw new InvalidOperationException("Original native security descriptor readback differs.");
    }
}
'@
}
try {
    if ($ResumeAbort -and $ResumeAfterAbort) { throw 'Select only one explicit continuation stage.' }
    if ($Apply -and (-not (Test-L4Administrator) -or $PSVersionTable.PSEdition -ne 'Desktop' -or ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess))) {
        function Literal([string]$Value) { return "'" + $Value.Replace("'", "''") + "'" }
        $resumeArguments = if ($ResumeAbort) { " -ResumeAbort -BrokerDirectoryPolicySha256 $(Literal $BrokerDirectoryPolicySha256)" } else { '' }
        if ($ResumeAfterAbort) { $resumeArguments = ' -ResumeAfterAbort' }
        if ($DiagnosticInventorySha256) { $resumeArguments += " -DiagnosticInventorySha256 $(Literal $DiagnosticInventorySha256)" }
        $script = "`$code = 1; & $(Literal $PSCommandPath) -PreviousOperation $(Literal $PreviousOperation) -PreviousVersion $(Literal $PreviousVersion) -PreviousInstallerSha256 $(Literal $PreviousInstallerSha256) -Bundle $(Literal ([IO.Path]::GetFullPath($Bundle))) -Version $(Literal $Version) -InstallerSha256 $(Literal $InstallerSha256) -Arch $(Literal $Arch) -Apply$resumeArguments; `$code = `$LASTEXITCODE; Read-Host 'Press Enter to close'; exit `$code"
        $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) { $powershell = Join-Path $env:SystemRoot 'Sysnative\WindowsPowerShell\v1.0\powershell.exe' }
        $launch = @{ FilePath = $powershell; WindowStyle = 'Normal'; ArgumentList = @('-NoProfile', '-EncodedCommand', [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($script))); Wait = $true; PassThru = $true }
        if (-not (Test-L4Administrator)) { $launch.Verb = 'RunAs' }
        $child = Start-Process @launch; $code = $child.ExitCode; $child.Dispose(); exit $code
    }
    & $module {
        param($OldId, $OldVersion, $OldPin, $Bundle, $Version, $Pin, $Arch, $Apply, $ResumeAbort, $DirectoryPin, $ResumeAfterAbort, $DiagnosticPin)
        $holds = [Collections.Generic.List[IDisposable]]::new()
        try {
        $data = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'Leo4\Tools'
        $binaries = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'Leo4\Tools'
        $operation = Join-Path $data "update\operations\$OldId"
        $oldExe = Join-Path $binaries "setup\$OldVersion\l4setup.exe"
        $release = Join-Path $binaries "releases\$OldVersion"
        foreach ($path in @($operation, $oldExe, $Bundle)) { Assert-L4NoReparse $path }
        if ((Get-FileHash -LiteralPath $oldExe -Algorithm SHA256).Hash -ine $OldPin -or (Get-AuthenticodeSignature -LiteralPath $oldExe).Status -ne 'Valid') { throw 'Original signed installer pin refused.' }
        $newExe = Join-Path ([IO.Path]::GetFullPath($Bundle)) 'l4setup.exe'
        Assert-L4NoReparse $newExe
        foreach ($file in @('l4setup.exe', 'setup-catalog.json', 'setup-catalog.json.sig', 'l4tools-release.json', 'l4tools-release.json.sig', "l4tools-layout-$Arch.json", "l4tools-layout-$Arch.json.sig", "l4tools-layout-$Arch.zip")) {
            $path = Join-Path $Bundle $file; Assert-L4NoReparse $path
            $holds.Add([IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read))
        }
        $holds.Add([IO.File]::Open($oldExe, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read))
        if ((Get-FileHash -LiteralPath $newExe -Algorithm SHA256).Hash -ine $Pin -or (Get-AuthenticodeSignature -LiteralPath $newExe).Status -ne 'Valid') { throw 'New signed installer pin refused.' }
        $statusText = & $oldExe --fresh-status --fresh-version $OldVersion --operation $OldId --arch $Arch | Out-String
        if ($LASTEXITCODE -ne 0) { throw 'Native original status refused.' }
        $status = ($statusText.Trim()) | ConvertFrom-Json
        if ($status.committed -or -not $status.receipt -or ($status.aborted -and -not ($ResumeAbort -or $ResumeAfterAbort)) -or ($ResumeAbort -and (-not $status.aborted -or -not $DirectoryPin)) -or ($ResumeAfterAbort -and -not $status.aborted)) { throw 'Original native state does not authorize the requested repair stage.' }
        # Native status verified the complete journal chain. Read config plans
        # without changing the original journal or adopting new process records.
        $journal = [IO.File]::ReadAllBytes((Join-Path $operation 'journal.bin'))
        $plans = @(); $broker = $null; $bootstrap = [uint64]0; $commands = @{}; $undoDone = @{}; $at = 24
        function Read-JournalString([byte[]]$Bytes, [ref]$Cursor, [int]$End) {
            if ($Cursor.Value + 4 -gt $End) { throw 'Truncated bootstrap string.' }
            $n = [BitConverter]::ToUInt32($Bytes, $Cursor.Value); $Cursor.Value += 4
            if (-not $n -or $n -gt 8192 -or $Cursor.Value + $n -gt $End) { throw 'Invalid bootstrap string.' }
            $value = [Text.UTF8Encoding]::new($false, $true).GetString($Bytes, $Cursor.Value, $n)
            if ($value.Contains([string][char]0)) { throw 'NUL in bootstrap string.' }
            $Cursor.Value += $n; return $value
        }
        while ($at -lt $journal.Length) {
            if ($at + 52 -gt $journal.Length) { throw 'Incomplete journal frame.' }
            $kind = [BitConverter]::ToUInt32($journal, $at); $size = [BitConverter]::ToUInt32($journal, $at + 4); $seq = [BitConverter]::ToUInt64($journal, $at + 8)
            $start = $at + 48
            if ($size -gt 65536 -or $start + $size + 4 -gt $journal.Length) { throw 'Unreviewed journal frame.' }
            if ($kind -eq 40) {
                if ($bootstrap -or [BitConverter]::ToUInt32($journal, $start) -ne 1) { throw 'Invalid bootstrap plan.' }; $bootstrap = $seq
                $cursor = $start + 4; $end = $start + $size
                if ((Read-JournalString $journal ([ref]$cursor) $end) -cne $binaries -or (Read-JournalString $journal ([ref]$cursor) $end) -cne $data -or (Read-JournalString $journal ([ref]$cursor) $end) -cne $OldVersion) { throw 'Original bootstrap roots/version mismatch.' }
                for ($i = 0; $i -lt 4; $i++) {
                    $name = Read-JournalString $journal ([ref]$cursor) $end
                    if ($name -notin $script:Names -or $commands.ContainsKey($name)) { throw 'Unsupported bootstrap service set.' }
                    $commands[$name] = Read-JournalString $journal ([ref]$cursor) $end
                    if ($cursor + 44 -gt $end -or [BitConverter]::ToUInt32($journal, $cursor) -ne 2) { throw 'Unsupported selected start type.' }
                    $cursor += 44
                }
                if ($cursor -ne $end) { throw 'Trailing bootstrap bytes.' }
            }
            if ($kind -eq 20) {
                if ($size -lt 24 -or [BitConverter]::ToUInt32($journal, $start) -ne 1 -or [BitConverter]::ToUInt32($journal, $start + 4) -ne 0) { throw 'Only initially absent configs can be repaired.' }
                $pn = [BitConverter]::ToUInt32($journal, $start + 8); $on = [BitConverter]::ToUInt32($journal, $start + 12); $nn = [BitConverter]::ToUInt32($journal, $start + 16); $sn = [BitConverter]::ToUInt32($journal, $start + 20)
                if ($on -or 24 + $pn + $nn + $sn -ne $size) { throw 'Invalid config plan.' }
                $relative = [Text.Encoding]::UTF8.GetString($journal, $start + 24, $pn)
                if ($relative.Contains('..') -or $relative.Contains(':') -or $relative.StartsWith('\') -or $relative.StartsWith('/')) { throw 'Unsafe config plan path.' }
                $bytes = [byte[]]::new($nn); [Array]::Copy($journal, $start + 24 + $pn, $bytes, 0, $nn)
                $sd = [byte[]]::new($sn); [Array]::Copy($journal, $start + 24 + $pn + $nn, $sd, 0, $sn)
                $plan = [pscustomobject]@{ Sequence = $seq; Relative = $relative; Bytes = $bytes; Security = $sd }
                $plans += $plan
                if ($relative -eq 'mosquitto\mosquitto.conf') { if ($broker) { throw 'Duplicate broker plan.' }; $broker = $plan }
            }
            if ($kind -eq 22 -and $size -eq 12 -and [BitConverter]::ToUInt32($journal, $start + 8) -eq 0) {
                $undoDone[[BitConverter]::ToUInt64($journal, $start)] = $true
            }
            $at += 52 + $size
        }
        if (-not $bootstrap -or -not $broker -or $plans.Count -ne 13) { throw 'Unsupported original operation shape.' }
        $descriptor = Get-Content -LiteralPath (Join-Path $operation "inputs\l4tools-layout-$Arch.json") -Raw | ConvertFrom-Json
        if ($descriptor.version -ne $OldVersion -or $descriptor.arch -ne $Arch) { throw 'Original descriptor mismatch.' }
        $nextDescriptor = Get-Content -LiteralPath (Join-Path $Bundle "l4tools-layout-$Arch.json") -Raw | ConvertFrom-Json
        $nextLauncher = @($nextDescriptor.files | Where-Object { $_.path -ceq 'l4launch/l4launch.exe' })
        if ($nextDescriptor.version -ne $Version -or $nextDescriptor.arch -ne $Arch -or $nextLauncher.Count -ne 1) { throw 'Candidate launcher descriptor mismatch.' }
        $bin = Join-Path $binaries 'bin'
        Assert-L4FreshLaunchers $bin $nextLauncher[0].sha256 $nextLauncher[0].size
        function Observe([string]$Name) {
            $values = @(Get-CimInstance Win32_Service -Filter "Name='$Name'")
            if ($values.Count -ne 1) { throw 'Original service set is incomplete.' }; $s = $values[0]
            $component = switch ($Name) { 'L4Superv' { 'l4superv' }; 'L4Con' { 'l4con' }; 'Leo4Proxy' { 'leo4proxy' }; 'mosquitto' { 'mosquitto' } }
            $image = Join-Path $release "$component\$component.exe"
            $prefix = '"' + $image + '"'
            if ($s.DisplayName -cne "$Name [L4:${OldId}:$bootstrap]" -or $s.StartName -ne 'LocalSystem' -or $s.StartMode -ne 'Manual' -or $s.ServiceType -ne 'Own Process' -or $s.PathName -cne $commands[$Name] -or -not ($s.PathName -ceq $prefix -or $s.PathName.StartsWith($prefix + ' ', [StringComparison]::Ordinal))) { throw 'Foreign or finalized service refused.' }
            Assert-L4NoReparse $image
            $files = @($descriptor.files | Where-Object { $_.path -ceq "$component/$component.exe" })
            if ($files.Count -ne 1 -or (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash -ine $files[0].sha256) { throw 'Original service image hash mismatch.' }
            $created = [long]0
            if ($s.ProcessId) { $p = [Diagnostics.Process]::GetProcessById($s.ProcessId); try { $created = $p.StartTime.ToUniversalTime().Ticks } finally { $p.Dispose() } }
            $s | Add-Member -NotePropertyName CreationTicks -NotePropertyValue $created
            return $s
        }
        function Assert-NoSuiteServices {
            foreach ($name in $script:Names) {
                if (@(Get-CimInstance Win32_Service -Filter "Name='$name'").Count) { throw 'Abort continuation requires all four original services absent.' }
            }
            if (@(Get-Process l4con, leo4proxy, mosquitto, l4superv, l4desk, ffmpeg -ErrorAction SilentlyContinue).Count) { throw 'Abort continuation refuses active suite processes.' }
        }
        $services = @()
        if ($ResumeAbort -or $ResumeAfterAbort) { Assert-NoSuiteServices }
        else {
            $services = @($script:Names | ForEach-Object { Observe $_ })
            if ($services[0].State -ne 'Stopped' -or $services[0].ProcessId -ne 0) { throw 'Supervisor must already be stopped; no live supervisor repair.' }
        }
        $diagnosticNames = @('config\mosquitto\mosquitto.conf.previous', 'state\state.json', 'state\l4desk\ffmpeg_state.json', 'state\leo4proxy\policy.json', 'logs\l4desk\l4desk.log', 'logs\mosquitto\mosquitto.log')
        $known = @($plans | ForEach-Object { 'config\' + $_.Relative }) + $diagnosticNames
        $runtimeFiles = @()
        foreach ($area in @('config', 'state', 'logs')) {
            $source = Join-Path $data $area; Assert-L4NoReparse $source
            $pending = [Collections.Generic.Stack[string]]::new(); $pending.Push($source)
            while ($pending.Count) {
                foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
                    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Mutable tree has a reparse point.' }
                    if ($item.PSIsContainer) { $pending.Push($item.FullName) }
                    else {
                        if ($known -notcontains $item.FullName.Substring($data.Length + 1)) { throw 'Unreviewed runtime file; repair refused.' }
                        $runtimeFiles += $item.FullName
                    }
                }
            }
        }
        $brokerPath = Join-Path $data 'config\mosquitto\mosquitto.conf'
        $brokerDirectory = Split-Path -Parent $brokerPath
        function Assert-DirectoryPin {
            Assert-L4NoReparse $brokerDirectory
            $hash = [Security.Cryptography.SHA256]::Create()
            try { $actual = [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes((Get-Acl -LiteralPath $brokerDirectory).Sddl))).Replace('-', '') }
            finally { $hash.Dispose() }
            if ($actual -ine $DirectoryPin) { throw 'Inspected broker directory policy changed; repair refused.' }
        }
        if ($ResumeAbort) {
            Assert-DirectoryPin
            # Only exact original candidate bytes or already removed fresh files
            # may remain. The signed native recovery validates SD and journal.
            foreach ($plan in $plans) {
                $file = Join-Path (Join-Path $data 'config') $plan.Relative
                if (Test-Path -LiteralPath $file) {
                    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($file)) -cne [Convert]::ToBase64String($plan.Bytes)) { throw 'Remaining config differs from original prepared candidate.' }
                }
            }
        }
        function Assert-ConfigCleanup {
            foreach ($plan in $plans) {
                if (Test-Path -LiteralPath (Join-Path (Join-Path $data 'config') $plan.Relative)) { throw 'Native bootstrap abort did not complete configuration cleanup.' }
            }
        }
        if ($ResumeAfterAbort) { Assert-ConfigCleanup }
        if ($ResumeAfterAbort) {
            foreach ($plan in $plans) {
                if (-not $undoDone.ContainsKey($plan.Sequence)) { throw 'Original configuration rollback completion is not recorded.' }
            }
        }
        $destination = Join-Path $operation 'failed-runtime'
        function Assert-DiagnosticDestination {
            if (Test-Path -LiteralPath $destination) {
                Assert-L4NoReparse $destination
                if ((Get-Acl -LiteralPath $destination).Sddl -cne (Get-Acl -LiteralPath $operation).Sddl) { throw 'Diagnostic destination security differs from private operation.' }
                if (@(Get-ChildItem -LiteralPath $destination -Force).Count) {
                    # Explicit completion of an independently inspected six-file
                    # diagnostic retention. Never infer completion from existence.
                    if (-not $ResumeAfterAbort -or -not $DiagnosticPin -or $runtimeFiles.Count) { throw 'Partial diagnostic move exists; inspect before continuing.' }
                    $found = @(); $stack = [Collections.Generic.Stack[string]]::new(); $stack.Push($destination)
                    while ($stack.Count) {
                        foreach ($entry in Get-ChildItem -LiteralPath $stack.Pop() -Force) {
                            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse diagnostic entry refused.' }
                            if ($entry.PSIsContainer) { $stack.Push($entry.FullName) }
                            else {
                                $relative = $entry.FullName.Substring($destination.Length + 1)
                                if ($diagnosticNames -notcontains $relative) { throw 'Unreviewed retained diagnostic file.' }
                                # AI is Windows inheritance bookkeeping, not an
                                # additional ACE or permission (native journal
                                # applies the same normalization).
                                $security = (Get-Acl -LiteralPath $entry.FullName).Sddl.Replace('D:PAI(', 'D:P(')
                                if ($security -cne 'O:BAG:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)') { throw 'Retained diagnostic file is not private to controllers.' }
                                $found += $relative
                            }
                        }
                    }
                    if ($found.Count -ne $diagnosticNames.Count) { throw 'Retained diagnostic inventory incomplete.' }
                    $lines = @($diagnosticNames | Sort-Object | ForEach-Object { $_ + ':' + (Get-FileHash -LiteralPath (Join-Path $destination $_) -Algorithm SHA256).Hash.ToLowerInvariant() })
                    $hash = [Security.Cryptography.SHA256]::Create()
                    try { $actual = [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes(($lines -join "`n")))).Replace('-', '') }
                    finally { $hash.Dispose() }
                    if ($actual -ine $DiagnosticPin) { throw 'Retained diagnostic inventory hash differs from inspected pin.' }
                } elseif ($DiagnosticPin) { throw 'Pinned retained diagnostic inventory is absent.' }
            }
        }
        Assert-DiagnosticDestination
        $observedBroker = if ($ResumeAfterAbort) { '' } else { (Get-FileHash -LiteralPath $brokerPath -Algorithm SHA256).Hash }
        $hasher = [Security.Cryptography.SHA256]::Create()
        try { $expectedBroker = [BitConverter]::ToString($hasher.ComputeHash($broker.Bytes)).Replace('-', '') }
        finally { $hasher.Dispose() }
        if (-not $Apply) {
            if ($ResumeAfterAbort -and $DiagnosticPin) { Assert-L4NativeFreshData $data }
            [pscustomobject]@{ PreviousOperation = $OldId; NativeCommitted = $false; BootstrapAborted = $status.aborted; ResumeConfigAbort = $ResumeAbort; ResumeAfterAbort = $ResumeAfterAbort; OriginalBootstrap = $bootstrap; CurrentPids = @($services | Select-Object Name, State, ProcessId); RemainingDiagnosticFiles = $runtimeFiles.Count; NextVersion = $Version; ApplyRequested = $false } | ConvertTo-Json -Depth 5
            return
        }
        if (-not (Test-L4Administrator) -or $PSVersionTable.PSEdition -ne 'Desktop') { throw 'Native elevated Windows PowerShell required.' }
        Invoke-L4Native $newExe @('--fresh-verify', '--bundle', $Bundle, '--fresh-version', $Version, '--arch', $Arch)
        # Authenticate original inputs under SYSTEM before operator stop/repair.
        Invoke-L4Native $oldExe @('--fresh-verify', '--bundle', (Join-Path $operation 'inputs'), '--fresh-version', $OldVersion, '--arch', $Arch)
        foreach ($original in $services | Select-Object -Skip 1) {
            $current = Observe $original.Name
            if ($current.State -ne 'Running' -or $current.ProcessId -ne $original.ProcessId -or $current.CreationTicks -ne $original.CreationTicks -or $current.PathName -cne $original.PathName) { throw 'Inspected epoch changed before repair.' }
            $process = [Diagnostics.Process]::GetProcessById($current.ProcessId)
            $controller = [System.ServiceProcess.ServiceController]::new($current.Name)
            try {
                $null = $process.Handle
                if ($process.StartTime.ToUniversalTime().Ticks -ne $original.CreationTicks) { throw 'Held epoch changed before repair.' }
                if (-not [string]::Equals($process.MainModule.FileName, (Join-Path $release "$($process.ProcessName)\$($process.ProcessName).exe"), [StringComparison]::OrdinalIgnoreCase)) { throw 'Held process image mismatch.' }
                foreach ($dependent in $controller.DependentServices) { try { if ($dependent.ServiceName -notin $script:Names) { throw 'Foreign dependent refused.' } } finally { $dependent.Dispose() } }
                $seconds = if ($current.Name -eq 'mosquitto') { 300 } else { 120 }; $watch = [Diagnostics.Stopwatch]::StartNew()
                Write-Host "Operator repair: stopping $($current.Name)..."
                $controller.Stop(); $controller.WaitForStatus([System.ServiceProcess.ServiceControllerStatus]::Stopped, [TimeSpan]::FromSeconds($seconds))
                if (-not $process.WaitForExit([Math]::Max(0, [int](1000 * $seconds - $watch.ElapsedMilliseconds)))) { throw 'Original inspected process has not exited.' }
            } finally { $controller.Dispose(); $process.Dispose() }
        }
        Wait-L4PortsFree
        if ($ResumeAbort) {
            Assert-NoSuiteServices; Assert-DirectoryPin
            # Explicit operator repair of the pinned drifted private directory.
            # Narrow to the existing SYSTEM/Administrators controller policy;
            # never grant access or weaken native journal/config validation.
            $policy = [Security.AccessControl.DirectorySecurity]::new()
            $policy.SetSecurityDescriptorSddlForm('O:BAG:SYD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)')
            Set-Acl -LiteralPath $brokerDirectory -AclObject $policy
        }
        if (-not $ResumeAfterAbort) {
        if ((Get-FileHash -LiteralPath $brokerPath -Algorithm SHA256).Hash -cne $observedBroker) { throw 'Broker config changed since operator inspection.' }
        # Restore the original accepted candidate, not a new journal/epoch record.
        [IO.File]::WriteAllBytes($brokerPath, $broker.Bytes)
        # Preserve the exact native SD representation. Managed Set-Acl changes
        # inherited-policy representation and fails the native journal guard.
        [L4FreshRepairSecurity]::Restore($brokerPath, $broker.Security)
        } else { Assert-NoSuiteServices; Assert-ConfigCleanup }
        # A fully recorded configuration rollback needs no replay. Its parent
        # directories may already have been removed for fresh admission.
        if (-not $ResumeAfterAbort) {
            Invoke-L4Native $oldExe @('--fresh-recover', '--fresh-version', $OldVersion, '--operation', $OldId, '--arch', $Arch)
        }
        $statusText = & $oldExe --fresh-status --fresh-version $OldVersion --operation $OldId --arch $Arch | Out-String
        if ($LASTEXITCODE -ne 0) { throw 'Cannot confirm original abort.' }
        $terminal = ($statusText.Trim()) | ConvertFrom-Json
        if (-not $terminal.aborted -or $terminal.committed) { throw 'Original abort is not complete.' }
        Assert-ConfigCleanup
        # Retain only this failed new runtime under its original private operation.
        # Sources/destinations are fixed absolute paths inside the same data root.
        Assert-DiagnosticDestination
        $acl = Get-Acl -LiteralPath $operation
        ([IO.DirectoryInfo]::new($destination)).Create($acl)
        # Move only reviewed residual files. Keep empty runtime directories in
        # place: renaming a whole tree can fail while a directory reader is open.
        foreach ($source in $runtimeFiles) {
            if (-not (Test-Path -LiteralPath $source)) { continue } # Native undo removed its own planned configs.
            Move-L4DiagnosticFile $data $operation $source
        }
        # Native data_empty rejects directory entries too. Original recovery
        # must run first because its config plans still need those parent paths.
        Assert-NoSuiteServices
        Remove-L4EmptyRuntimeDirectories $data
        Assert-L4NativeFreshData $data
        Assert-L4FreshLaunchers $bin $nextLauncher[0].sha256 $nextLauncher[0].size
        $next = [Guid]::NewGuid().ToString(); Write-Host "New clean installation operation: $next"
        Invoke-L4Native $newExe @('--fresh-install', '--bundle', $Bundle, '--fresh-version', $Version, '--arch', $Arch, '--operation', $next)
        Remove-L4RetiredDirectory
        Write-Host 'New installation completed; retired legacy directory removed.'
        } finally { foreach ($hold in $holds) { $hold.Dispose() } }
    } $PreviousOperation $PreviousVersion $PreviousInstallerSha256 $Bundle $Version $InstallerSha256 $Arch ([bool]$Apply) ([bool]$ResumeAbort) $BrokerDirectoryPolicySha256 ([bool]$ResumeAfterAbort) $DiagnosticInventorySha256
    exit 0
} catch { Write-Error -ErrorRecord $_ -ErrorAction Continue; exit 1 }
