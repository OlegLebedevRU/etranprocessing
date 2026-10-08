Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Windows PowerShell -NoProfile does not load this assembly implicitly via CIM.
# Load before admission or any retirement; absence must fail at module import.
Add-Type -AssemblyName System.ServiceProcess -ErrorAction Stop
$script:Names = @('L4Superv', 'L4Con', 'mosquitto', 'Leo4Proxy')
$script:Images = @{
    L4Superv = 'C:\l4tools\l4superv\l4superv.exe'
    L4Con = 'C:\l4tools\l4con\l4con.exe'
    mosquitto = 'C:\l4tools\mosquitto\mosquitto.exe'
    Leo4Proxy = 'C:\l4tools\leo4proxy\leo4proxy.exe'
}

function Test-L4Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try { return ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
    finally { $identity.Dispose() }
}
function Open-L4NativeRegistry {
    return [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
}
function Get-L4MachinePath {
    $base = Open-L4NativeRegistry
    $key = $null
    try {
        $key = $base.OpenSubKey('SYSTEM\CurrentControlSet\Control\Session Manager\Environment', $false)
        if (-not $key -or $key.GetValueNames() -notcontains 'Path') { throw 'Machine PATH is absent.' }
        $kind = $key.GetValueKind('Path')
        if ($kind -notin @([Microsoft.Win32.RegistryValueKind]::String, [Microsoft.Win32.RegistryValueKind]::ExpandString)) { throw 'Unsupported machine PATH type.' }
        return [pscustomobject]@{ Value = $key.GetValue('Path', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames); Kind = [int]$kind }
    } finally { if ($key) { $key.Dispose() }; $base.Dispose() }
}
function Split-L4OldPath([string]$Value) {
    $known = @('C:\l4tools', 'C:\l4tools\l4sql', 'C:\l4tools\l4pin', 'C:\l4tools\l4con', 'C:\l4tools\l4superv', 'C:\l4tools\l4desk', 'C:\l4tools\ffmpeg', 'C:\l4tools\l4capture\bin', 'C:\l4tools\leo4proxy', 'C:\l4tools\mosquitto', 'C:\l4tools\l4media', 'C:\l4tools\l4capture')
    $keep = [Collections.Generic.List[string]]::new()
    $remove = [Collections.Generic.List[string]]::new()
    foreach ($entry in $Value.Split(';')) {
        $normal = $entry.Trim().Trim('"').TrimEnd('\')
        if ($known -contains $normal) { $remove.Add($entry) }
        elseif (($normal -match '(?i)^C:\\l4tools(?:\\|$)') -or (($normal -match '(?i)l4tools') -and $normal.Contains('%'))) {
            throw 'Unreviewed legacy PATH entry; review manually before transition.'
        } else { $keep.Add($entry) }
    }
    return [pscustomobject]@{ Original = $Value; Value = [string]::Join(';', $keep); Removed = @($remove.ToArray()) }
}
function Assert-L4NoReparse([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    while ($item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse path refused.' }
        if ($item -is [IO.FileInfo]) { $item = $item.Directory } else { $item = $item.Parent }
    }
}
function Move-L4DiagnosticFile([string]$DataRoot, [string]$OperationRoot, [string]$Source) {
    $data = [IO.Path]::GetFullPath($DataRoot).TrimEnd('\')
    $operation = [IO.Path]::GetFullPath($OperationRoot).TrimEnd('\')
    $sourcePath = [IO.Path]::GetFullPath($Source)
    if (-not $operation.StartsWith($data + '\', [StringComparison]::OrdinalIgnoreCase) -or -not $sourcePath.StartsWith($data + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Diagnostic move escaped original operation.' }
    $relative = $sourcePath.Substring($data.Length + 1)
    if ($relative -notmatch '^(config|state|logs)\\') { throw 'Only reviewed runtime areas can be retained.' }
    Assert-L4NoReparse $sourcePath
    if ((Get-Item -LiteralPath $sourcePath -Force).PSIsContainer) { throw 'Diagnostic move accepts files only.' }
    $destination = Join-Path $operation 'failed-runtime'
    Assert-L4NoReparse $destination
    $target = [IO.Path]::GetFullPath((Join-Path $destination $relative))
    if (-not $target.StartsWith($destination + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Diagnostic target escaped original operation.' }
    $parent = [IO.Path]::GetDirectoryName($target)
    $existing = $parent
    while (-not (Test-Path -LiteralPath $existing)) { $existing = [IO.Path]::GetDirectoryName($existing) }
    Assert-L4NoReparse $existing
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    Assert-L4NoReparse $parent
    # Same-volume rename of one file, no overwrite or recursive directory move.
    [IO.File]::Move($sourcePath, $target)
}
function Assert-L4FreshLaunchers([string]$BinRoot, [string]$ExpectedHash, [long]$ExpectedSize) {
    Assert-L4NoReparse $BinRoot
    $names = @('leo4proxy', 'mosquitto', 'l4con', 'l4superv', 'l4desk', 'l4sql', 'l4pin', 'l4capture', 'ffmpeg') | ForEach-Object { $_ + '.exe' }
    foreach ($item in Get-ChildItem -LiteralPath $BinRoot -Force) {
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $names -notcontains $item.Name) { throw 'Unreviewed launcher entry refused.' }
        if ($item.Length -ne $ExpectedSize -or (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash -ine $ExpectedHash) { throw 'Existing launcher differs from candidate; inspect and retire original launcher before fresh install.' }
    }
}
function Assert-L4NativeFreshData([string]$DataRoot) {
    $data = [IO.Path]::GetFullPath($DataRoot).TrimEnd('\')
    foreach ($area in @('config', 'state', 'logs')) {
        $root = Join-Path $data $area
        Assert-L4NoReparse $root
        if (@(Get-ChildItem -LiteralPath $root -Force).Count) { throw 'Native fresh-install root contains entries, including empty directories.' }
    }
    $operations = Join-Path $data 'update\operations'
    Assert-L4NoReparse $operations
    if (Test-Path -LiteralPath (Join-Path $operations 'update.state')) { throw 'Native fresh-install refuses retained update.state; original-operation inspection required.' }
}
function Remove-L4EmptyRuntimeDirectories([string]$DataRoot) {
    $data = [IO.Path]::GetFullPath($DataRoot).TrimEnd('\')
    Assert-L4NoReparse $data
    $directories = [Collections.Generic.List[string]]::new()
    # Validate the entire inventory before any removal. Never remove a root,
    # follow a link or use recursive/forced deletion.
    foreach ($area in @('config', 'state', 'logs')) {
        $root = Join-Path $data $area
        Assert-L4NoReparse $root
        $pending = [Collections.Generic.Stack[string]]::new(); $pending.Push($root)
        while ($pending.Count) {
            foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
                if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or -not $item.PSIsContainer) { throw 'Fresh runtime must contain only empty real directories.' }
                $path = [IO.Path]::GetFullPath($item.FullName)
                if (-not $path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Runtime directory escaped its fixed root.' }
                if ($directories.Count -ge 256) { throw 'Unexpected runtime directory count.' }
                $directories.Add($path); $pending.Push($path)
            }
        }
    }
    foreach ($path in $directories | Sort-Object Length -Descending) {
        Assert-L4NoReparse $path
        [IO.Directory]::Delete($path, $false) # Atomic refusal if a file appears.
    }
    foreach ($area in @('config', 'state', 'logs')) {
        if (@(Get-ChildItem -LiteralPath (Join-Path $data $area) -Force).Count) { throw 'Native fresh-install root is not empty.' }
    }
}
function Assert-L4ServiceSnapshot($Service) {
    $expected = $script:Images[$Service.Name]
    if (-not $expected -or $Service.StartName -notin @('LocalSystem', 'NT AUTHORITY\SYSTEM') -or $Service.StartMode -ne 'Auto' -or $Service.ServiceType -ne 'Own Process') { throw 'Foreign service configuration refused.' }
    $command = [string]$Service.PathName
    if ($command -notmatch '^"([^"]+)"(?:\s+(.*))?$') { throw 'Only reviewed quoted legacy service commands are admitted.' }
    if (-not [string]::Equals($Matches[1], $expected, [StringComparison]::OrdinalIgnoreCase)) { throw 'Foreign service image refused.' }
    # Keep the exact original arguments in the private snapshot, never rebuild them.
}
function Get-L4ServiceSnapshot([string]$Name) {
    $values = @(Get-CimInstance Win32_Service -Filter "Name='$Name'")
    if ($values.Count -ne 1) { throw "Expected one original service: $Name" }
    $s = $values[0]
    $value = [pscustomobject]@{ Name = $s.Name; PathName = $s.PathName; StartName = $s.StartName; StartMode = $s.StartMode; ServiceType = $s.ServiceType; State = $s.State; ProcessId = [int]$s.ProcessId; CreationUtc = $null; Image = $script:Images[$Name]; Sha256 = $null }
    Assert-L4ServiceSnapshot $value
    Assert-L4NoReparse $value.Image
    $value.Sha256 = (Get-FileHash -LiteralPath $value.Image -Algorithm SHA256).Hash
    if ($value.ProcessId -gt 0) {
        $process = [Diagnostics.Process]::GetProcessById($value.ProcessId)
        try { $value.CreationUtc = $process.StartTime.ToUniversalTime().Ticks }
        finally { $process.Dispose() }
    }
    return $value
}
function Assert-L4SameService($Original, $Current, [switch]$Stopped) {
    foreach ($field in @('Name', 'PathName', 'StartName', 'StartMode', 'ServiceType', 'Image', 'Sha256')) {
        if ($Original.$field -cne $Current.$field) { throw "Service fingerprint changed: $($Original.Name) / $field" }
    }
    if ($Stopped) {
        if ($Current.State -ne 'Stopped' -or $Current.ProcessId -ne 0) { throw 'Original service is not stopped.' }
    } elseif ($Current.State -ne 'Running' -or $Current.ProcessId -ne $Original.ProcessId -or $Current.CreationUtc -ne $Original.CreationUtc) { throw 'Original service epoch changed.' }
}
function Get-L4FreshTransitionPlan {
    [CmdletBinding()]
    param([string]$Bundle, [string]$Version, [string]$InstallerSha256, [string]$Arch = 'x86')
    if ($Bundle -notmatch '^[A-Za-z]:[\\/]' -or $Bundle.Contains('"') -or $Bundle.Substring(2).Contains(':') -or $Version -notmatch '^\d+\.\d+\.\d+$' -or $InstallerSha256 -notmatch '^[a-fA-F0-9]{64}$' -or $Arch -notin @('x86', 'x64')) { throw 'Invalid transition arguments.' }
    $full = [IO.Path]::GetFullPath($Bundle).TrimEnd('\')
    Assert-L4NoReparse $full
    $exe = Join-Path $full 'l4setup.exe'
    Assert-L4NoReparse $exe
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ine $InstallerSha256) { throw 'Installer differs from the independently reviewed SHA256.' }
    if ((Get-AuthenticodeSignature -LiteralPath $exe).Status -ne 'Valid') { throw 'Installer Authenticode is not valid.' }
    $services = @($script:Names | ForEach-Object { Get-L4ServiceSnapshot $_ })
    foreach ($s in $services) { if ($s.State -ne 'Running' -or $s.ProcessId -le 0) { throw 'All four original services must be running before beginning.' } }
    $path = Get-L4MachinePath
    $split = Split-L4OldPath $path.Value
    return [pscustomobject]@{ Bundle = $full; Version = $Version; Arch = $Arch; InstallerSha256 = $InstallerSha256; Installer = $exe; RetireOrder = $script:Names; Services = $services; Path = $path; PathCandidate = $split.Value; OldPathEntries = $split.Removed; ConnectionOutage = $true }
}
function Assert-L4BaselineIdle([string]$DataRoot) {
    $path = Join-Path $DataRoot 'update\operations\update.state'; Assert-L4NoReparse $path
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -ne 112) { throw 'Invalid update marker size.' }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = $sha.ComputeHash($bytes,0,80) } finally { $sha.Dispose() }
    if ([Text.Encoding]::ASCII.GetString($bytes,0,8) -cne "L4UPD01`0" -or [BitConverter]::ToUInt32($bytes,48) -or
        [BitConverter]::ToUInt32($bytes,52) -or [BitConverter]::ToUInt64($bytes,72) -or
        [Convert]::ToBase64String($digest) -cne [Convert]::ToBase64String($bytes[80..111])) { throw 'Active or corrupt update marker refused before retirement.' }
    if (-not [BitConverter]::ToUInt64($bytes,56)) {
        if (@($bytes[8..47] | Where-Object { $_ -ne 0 }).Count -or [BitConverter]::ToUInt64($bytes,64)) { throw 'Invalid initial clear marker.' }
    } else {
        $owner = [Text.Encoding]::ASCII.GetString($bytes,8,36)
        if ($owner -notmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$' -or [Guid]$owner -eq [Guid]::Empty -or
            -not [BitConverter]::ToUInt64($bytes,64) -or @($bytes[44..47] | Where-Object { $_ -ne 0 }).Count) { throw 'Invalid terminal clear marker.' }
    }
}
function Get-L4FreshBaselinePlan {
    [CmdletBinding()]
    param([string]$Bundle, [string]$Version, [string]$InstallerSha256,
        [string]$SourceBundle, [string]$SourceVersion, [string]$SourceInstallerSha256,
        [string]$OriginOperation, [string]$OriginVersion, [string]$OriginInstallerSha256,
        [string]$Arch = 'x86')
    if ($SourceVersion -notmatch '^\d+\.\d+\.\d+$' -or $OriginVersion -notmatch '^\d+\.\d+\.\d+$' -or
        $OriginOperation -notmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$' -or
        [Guid]$OriginOperation -eq [Guid]::Empty -or $OriginInstallerSha256 -notmatch '^[a-fA-F0-9]{64}$' -or
        $SourceInstallerSha256 -notmatch '^[a-fA-F0-9]{64}$' -or $SourceBundle -notmatch '^[A-Za-z]:[\\/]' -or
        $SourceBundle.Contains('"') -or $SourceBundle.Substring(2).Contains(':')) { throw 'Invalid explicit installed baseline identity.' }
    $binaries = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'Leo4\Tools'
    $release = Join-Path $binaries "releases\$SourceVersion"
    $images = @{}
    foreach ($pair in @(@('L4Superv','l4superv'), @('L4Con','l4con'), @('mosquitto','mosquitto'), @('Leo4Proxy','leo4proxy'))) {
        $images[$pair[0]] = Join-Path $release ($pair[1] + '\' + $pair[1] + '.exe')
    }
    $script:Images = $images
    $plan = Get-L4FreshTransitionPlan -Bundle $Bundle -Version $Version -InstallerSha256 $InstallerSha256 -Arch $Arch
    $source = [IO.Path]::GetFullPath($SourceBundle).TrimEnd('\')
    $sourceExe = Join-Path $source 'l4setup.exe'
    $originExe = Join-Path $binaries "setup\$OriginVersion\l4setup.exe"
    foreach ($entry in @(@($sourceExe,$SourceInstallerSha256), @($originExe,$OriginInstallerSha256))) {
        Assert-L4NoReparse $entry[0]
        if ((Get-FileHash -LiteralPath $entry[0] -Algorithm SHA256).Hash -ine $entry[1] -or
            (Get-AuthenticodeSignature -LiteralPath $entry[0]).Status -ne 'Valid') { throw 'Original signed installer fingerprint refused.' }
    }
    $statusText = & $originExe --fresh-status --fresh-version $OriginVersion --operation $OriginOperation --arch $Arch | Out-String
    if ($LASTEXITCODE -ne 0) { throw 'Original fresh receipt validation failed.' }
    $status = $statusText | ConvertFrom-Json
    if (-not $status.committed -or $status.aborted -or -not $status.receipt -or $status.operation_id -cne $OriginOperation) { throw 'Original installation is not a committed fresh origin.' }
    $inventory = Get-Content -LiteralPath (Join-Path $source "l4tools-layout-$Arch.json") -Raw | ConvertFrom-Json
    if ($inventory.version -cne $SourceVersion -or $inventory.arch -cne $Arch) { throw 'Source descriptor identity differs.' }
    foreach ($service in $plan.Services) {
        $relative = $service.Image.Substring($release.Length + 1).Replace('\','/')
        $asset = @($inventory.files | Where-Object { $_.path -ceq $relative })
        if ($asset.Count -ne 1 -or $asset[0].sha256 -ine $service.Sha256 -or $asset[0].size -ne (Get-Item -LiteralPath $service.Image).Length) { throw 'Installed service differs from source inventory.' }
    }
    $plan | Add-Member Kind 'installed-baseline'
    $plan | Add-Member SourceBundle $source
    $plan | Add-Member SourceVersion $SourceVersion
    $plan | Add-Member SourceInstaller $sourceExe
    $plan | Add-Member SourceInstallerSha256 $SourceInstallerSha256
    $plan | Add-Member OriginOperation $OriginOperation
    $plan | Add-Member OriginVersion $OriginVersion
    $plan | Add-Member OriginInstallerSha256 $OriginInstallerSha256
    return $plan
}
function Invoke-L4BaselineRetention($Plan, [string]$Record) {
    $helper = Join-Path $PSScriptRoot 'Retain-L4BaselineRuntime.ps1'
    Assert-L4NoReparse $helper
    $hold = [IO.File]::Open($helper, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $taskName = 'L4.OperatorBaseline.' + [Guid]::NewGuid().ToString()
    $result = Join-Path $Record ($taskName + '.json')
    function Quote-L4Retention([string]$Value) { return "'" + $Value.Replace("'", "''") + "'" }
    $command = "& $(Quote-L4Retention $helper) -Record $(Quote-L4Retention $Record) -SourceVersion $(Quote-L4Retention $Plan.SourceVersion) -Result $(Quote-L4Retention $result)"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $task = $null
    try {
        $service = New-Object -ComObject 'Schedule.Service'; $service.Connect(); $folder = $service.GetFolder('\')
        $definition = $service.NewTask(0)
        $definition.Principal.UserId = 'SYSTEM'; $definition.Principal.LogonType = 5; $definition.Principal.RunLevel = 1
        $definition.Settings.ExecutionTimeLimit = 'PT2M'; $definition.Settings.AllowDemandStart = $true
        $action = $definition.Actions.Create(0); $action.Path = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $action.Arguments = '-NoProfile -NonInteractive -EncodedCommand ' + $encoded
        $task = $folder.RegisterTaskDefinition($taskName, $definition, 2, 'SYSTEM', $null, 5, 'D:P(A;;FA;;;SY)(A;;FA;;;BA)')
        $null = $task.Run($null)
        $deadline = [DateTime]::UtcNow.AddSeconds(130)
        while (-not (Test-Path -LiteralPath $result)) {
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Baseline retention timed out; inspect original record.' }
            Start-Sleep -Milliseconds 200
        }
        $value = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json
        if (-not $value.success) { throw ('Baseline retention refused: ' + $value.error) }
        while ($task.State -eq 4) {
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Retention task has not exited.' }
            Start-Sleep -Milliseconds 100
        }
        if ($task.LastTaskResult -ne 0) { throw 'Retention task failed after result write.' }
    } finally {
        if ($task -and $task.State -ne 4) {
            try { $folder.DeleteTask($taskName,0) }
            catch { Write-Warning ('Owned retention task cleanup failed: '+$taskName) }
        }
        $hold.Dispose()
    }
}
function New-L4PrivateRecord {
    $root = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'Leo4\Tools\operator-transition'
    # All existing ancestors must be real directories. New directories get an
    # explicit protected SYSTEM/Administrators ACL before any sensitive write.
    $parts = [Collections.Generic.List[string]]::new()
    $cursor = $root
    while (-not (Test-Path -LiteralPath $cursor)) { $parts.Insert(0, $cursor); $cursor = [IO.Path]::GetDirectoryName($cursor) }
    Assert-L4NoReparse $cursor
    $suiteRoot = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'Leo4\Tools'
    foreach ($existing in @($suiteRoot, $root)) {
        if (-not (Test-Path -LiteralPath $existing)) { continue }
        $security = Get-Acl -LiteralPath $existing
        $owner = $security.GetOwner([Security.Principal.SecurityIdentifier]).Value
        if ($owner -notin @('S-1-5-18', 'S-1-5-32-544')) { throw 'Foreign operation directory owner refused.' }
        foreach ($rule in $security.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])) {
            $unsafe = [Security.AccessControl.FileSystemRights]::Write -bor [Security.AccessControl.FileSystemRights]::Delete -bor [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles -bor [Security.AccessControl.FileSystemRights]::ChangePermissions -bor [Security.AccessControl.FileSystemRights]::TakeOwnership
            if ($rule.AccessControlType -eq 'Allow' -and $rule.IdentityReference.Value -notin @('S-1-5-18', 'S-1-5-32-544') -and ($rule.FileSystemRights -band $unsafe)) { throw 'Operation directory permits foreign writers.' }
        }
    }
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    $admins = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    $acl.SetOwner($admins)
    foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid), 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
    }
    foreach ($part in $parts) { $dir = [IO.DirectoryInfo]::new($part); $dir.Create($acl) }
    $folder = Join-Path $root ([Guid]::NewGuid().ToString())
    ([IO.DirectoryInfo]::new($folder)).Create($acl)
    Assert-L4NoReparse $folder
    return $folder
}
function Invoke-L4Native([string]$File, [string[]]$Arguments) {
    & $File @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Native command failed with exit $LASTEXITCODE." }
}
function Wait-L4PortsFree {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $occupied = @([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() | Where-Object { $_.Port -in @(18443, 18883, 1883) })
        if ($occupied.Count -eq 0) { return }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Old communication ports remain occupied; no process will be killed.'
}
function Remove-L4RetiredDirectory {
    $root = 'C:\l4tools'
    if (-not (Test-Path -LiteralPath $root)) { return }
    Assert-L4NoReparse $root
    if (-not [string]::Equals((Get-Item -LiteralPath $root).FullName.TrimEnd('\'), $root, [StringComparison]::OrdinalIgnoreCase)) { throw 'Legacy directory resolved outside its fixed root.' }
    # Refuse links anywhere before a recursive native PowerShell deletion.
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($root)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Legacy tree contains a reparse point; automatic deletion refused.' }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
        }
    }
    foreach ($process in Get-CimInstance Win32_Process) {
        if ($process.ExecutablePath -and $process.ExecutablePath.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'A process still uses the retired tree; automatic deletion refused.' }
    }
    Remove-Item -LiteralPath $root -Recurse -Force
    if (Test-Path -LiteralPath $root) { throw 'Legacy tree deletion did not complete.' }
}
function Invoke-L4FreshTransition {
    [CmdletBinding()]
    param($Plan)
    if (-not (Test-L4Administrator)) { throw 'Elevated interactive operator required.' }
    if ($PSVersionTable.PSEdition -ne 'Desktop') { throw 'Use Windows PowerShell 5.1 for application.' }
    if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) { throw 'Use native 64-bit Windows PowerShell.' }
    $handles = [Collections.Generic.List[IDisposable]]::new()
    $backup = $null
    $reservation = $null
    $installedBaseline = $Plan.PSObject.Properties.Name -contains 'Kind' -and $Plan.Kind -eq 'installed-baseline'
    try {
        # Keep all admission inputs and the independently pinned EXE read-only
        # across verify/retirement/install. No edits to the sealed release kit.
        $files = @('l4setup.exe', 'setup-catalog.json', 'setup-catalog.json.sig', 'l4tools-release.json', 'l4tools-release.json.sig', "l4tools-layout-$($Plan.Arch).json", "l4tools-layout-$($Plan.Arch).json.sig", "l4tools-layout-$($Plan.Arch).zip")
        if ($installedBaseline) { $files += @('l4tools-bootstrap.json','l4tools-bootstrap.json.sig',"l4rollback-$($Plan.Arch).exe") }
        foreach ($file in $files) {
            $path = Join-Path $Plan.Bundle $file
            Assert-L4NoReparse $path
            $handles.Add([IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read))
        }
        if ($installedBaseline) {
            foreach ($name in @('l4setup.exe','setup-catalog.json','setup-catalog.json.sig','l4tools-release.json','l4tools-release.json.sig',"l4tools-layout-$($Plan.Arch).json","l4tools-layout-$($Plan.Arch).json.sig","l4tools-layout-$($Plan.Arch).zip")) {
                $sourceFile = Join-Path $Plan.SourceBundle $name; Assert-L4NoReparse $sourceFile
                $handles.Add([IO.File]::Open($sourceFile,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
            }
            $originExe = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) "Leo4\Tools\setup\$($Plan.OriginVersion)\l4setup.exe"
            $handles.Add([IO.File]::Open($originExe,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
            $again = Get-L4FreshBaselinePlan -Bundle $Plan.Bundle -Version $Plan.Version -InstallerSha256 $Plan.InstallerSha256 -Arch $Plan.Arch -SourceBundle $Plan.SourceBundle -SourceVersion $Plan.SourceVersion -SourceInstallerSha256 $Plan.SourceInstallerSha256 -OriginOperation $Plan.OriginOperation -OriginVersion $Plan.OriginVersion -OriginInstallerSha256 $Plan.OriginInstallerSha256
            Invoke-L4Native $again.SourceInstaller @('--fresh-verify', '--bundle', $again.SourceBundle, '--fresh-version', $again.SourceVersion, '--arch', $again.Arch, '--operation', [Guid]::NewGuid().ToString())
        } else { $again = Get-L4FreshTransitionPlan -Bundle $Plan.Bundle -Version $Plan.Version -InstallerSha256 $Plan.InstallerSha256 -Arch $Plan.Arch }
        $verifyId = [Guid]::NewGuid().ToString()
        Write-Host 'Verifying signed kit under SYSTEM before stopping communication...'
        Invoke-L4Native $Plan.Installer @('--fresh-verify', '--bundle', $Plan.Bundle, '--fresh-version', $Plan.Version, '--arch', $Plan.Arch, '--operation', $verifyId)
        if ($installedBaseline) {
            $data = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'Leo4\Tools'
            $lockPath = Join-Path $data 'update\operations\deployment.lock'; Assert-L4NoReparse $lockPath
            $reservation = [IO.File]::Open($lockPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
            Assert-L4BaselineIdle $data
            $binaries = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'Leo4\Tools'
            foreach ($process in Get-CimInstance Win32_Process) {
                if ($process.ExecutablePath -and ($process.ExecutablePath.StartsWith($binaries+'\setup\',[StringComparison]::OrdinalIgnoreCase) -or $process.ExecutablePath.StartsWith($binaries+'\recovery\',[StringComparison]::OrdinalIgnoreCase))) { throw 'Updater/recovery process remains; baseline retirement refused.' }
            }
            foreach ($service in Get-CimInstance Win32_Service -Filter "Name LIKE 'L4UpdateHost_%'") {
                if ($service.State -ne 'Stopped') { throw 'Update controller remains active.' }
            }
        }
        foreach ($original in $again.Services) {
            Assert-L4SameService $original (Get-L4ServiceSnapshot $original.Name)
            $process = [Diagnostics.Process]::GetProcessById($original.ProcessId)
            $handles.Add($process)
            $null = $process.Handle  # Hold original epoch, never look up PID again.
            if ($process.HasExited -or $process.StartTime.ToUniversalTime().Ticks -ne $original.CreationUtc -or -not [string]::Equals($process.MainModule.FileName, $original.Image, [StringComparison]::OrdinalIgnoreCase)) { throw 'Original process image/epoch differs.' }
            $original | Add-Member -NotePropertyName HeldProcess -NotePropertyValue $process
            $controller = [ServiceProcess.ServiceController]::new($original.Name)
            try {
                foreach ($dependent in $controller.DependentServices) {
                    try { if ($dependent.ServiceName -notin $script:Names) { throw 'Foreign dependent service refused.' } }
                    finally { $dependent.Dispose() }
                }
            } finally { $controller.Dispose() }
        }
        $backup = New-L4PrivateRecord
        $installId = [Guid]::NewGuid().ToString()
        $state = [ordered]@{ Schema = 1; Stage = 'prepared'; VerifyOperation = $verifyId; InstallOperation = $installId; Version = $Plan.Version; Arch = $Plan.Arch; Bundle = $Plan.Bundle; InstallerSha256 = $Plan.InstallerSha256; Path = $again.Path; PathCandidate = $again.PathCandidate }
        $record = Join-Path $backup 'transition.json'
        $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $record -Encoding UTF8
        Write-Host "Private operation record: $backup"
        $state.Stage = 'retiring'
        $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $record -Encoding UTF8
        foreach ($original in $again.Services) {
            Assert-L4SameService $original (Get-L4ServiceSnapshot $original.Name)
            if ($original.HeldProcess.HasExited) { throw 'Original process exited before owned stop.' }
            Write-Host "Stopping $($original.Name)..."
            $seconds = if ($original.Name -eq 'mosquitto') { 300 } else { 120 }
            $watch = [Diagnostics.Stopwatch]::StartNew()
            $controller = [ServiceProcess.ServiceController]::new($original.Name)
            try {
                $controller.Stop()
                $controller.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped, [TimeSpan]::FromSeconds($seconds))
            } finally { $controller.Dispose() }
            $remaining = [Math]::Max(0, [int](1000 * $seconds - $watch.ElapsedMilliseconds))
            if (-not $original.HeldProcess.WaitForExit($remaining)) { throw 'Original process did not exit within its budget; no forced kill.' }
            Assert-L4SameService $original (Get-L4ServiceSnapshot $original.Name) -Stopped
            Invoke-L4Native (Join-Path $env:SystemRoot 'System32\sc.exe') @('delete', $original.Name)
            $deadline = [DateTime]::UtcNow.AddSeconds(30)
            while (@(Get-CimInstance Win32_Service -Filter "Name='$($original.Name)'").Count -ne 0) {
                if ([DateTime]::UtcNow -ge $deadline) { throw 'Service deletion is still pending.' }
                Start-Sleep -Milliseconds 250
            }
        }
        Wait-L4PortsFree
        if ($installedBaseline) {
            Assert-L4BaselineIdle $data
            $reservation.Dispose(); $reservation = $null # Con is absent; SYSTEM retention reacquires and rechecks.
            Invoke-L4BaselineRetention $again $backup
        }
        $base = Open-L4NativeRegistry
        $key = $null
        try {
            $key = $base.OpenSubKey('SYSTEM\CurrentControlSet\Control\Session Manager\Environment', $true)
            $current = Get-L4MachinePath
            if ($current.Kind -ne $again.Path.Kind -or $current.Value -cne $again.Path.Value) { throw 'Machine PATH changed externally; refusing replacement.' }
            $key.SetValue('Path', $again.PathCandidate, [Microsoft.Win32.RegistryValueKind]$again.Path.Kind)
            $key.Flush()
            $readback = Get-L4MachinePath
            if ($readback.Kind -ne $again.Path.Kind -or $readback.Value -cne $again.PathCandidate) { throw 'PATH readback differs; inspect private backup.' }
        } finally { if ($key) { $key.Dispose() }; $base.Dispose() }
        $state.Stage = 'installing'
        $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $record -Encoding UTF8
        Write-Host "Fresh install operation: $installId"
        Invoke-L4Native $Plan.Installer @('--fresh-install', '--bundle', $Plan.Bundle, '--fresh-version', $Plan.Version, '--arch', $Plan.Arch, '--operation', $installId)
        $state.Stage = 'installer-success'
        $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $record -Encoding UTF8
        if (-not $installedBaseline) { Write-Host 'Installation completed. Removing retired C:\l4tools...'; Remove-L4RetiredDirectory }
        $state.Stage = 'complete'
        $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $record -Encoding UTF8
        Write-Host 'Transition completed. No legacy files or configuration were migrated or archived.'
    } catch {
        if ($backup) { Write-Warning "Transition stopped. Inspect $backup and the original install UUID. No automatic legacy restore/adoption or forced process kill." }
        throw
    } finally { if ($reservation) { $reservation.Dispose() }; foreach ($handle in $handles) { $handle.Dispose() } }
}
Export-ModuleMember -Function Get-L4FreshTransitionPlan, Get-L4FreshBaselinePlan, Invoke-L4FreshTransition, Test-L4Administrator
