#requires -Version 5.1
# Explicit one-time cold baseline preparation, run only by the operator's SYSTEM task.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Record,
    [Parameter(Mandatory=$true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$SourceVersion,
    [Parameter(Mandatory=$true)][string]$Result)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function RealPath([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    while ($item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse point refused.' }
        if ($item -is [IO.FileInfo]) { $item = $item.Directory } else { $item = $item.Parent }
    }
}
function PrivatePath([string]$Path) {
    RealPath $Path
    $acl = Get-Acl -LiteralPath $Path
    if ($acl.GetOwner([Security.Principal.SecurityIdentifier]).Value -notin @('S-1-5-18','S-1-5-32-544')) { throw 'Foreign owner refused.' }
    foreach ($rule in $acl.GetAccessRules($true,$true,[Security.Principal.SecurityIdentifier])) {
        $writes = [Security.AccessControl.FileSystemRights]::Write -bor [Security.AccessControl.FileSystemRights]::Delete -bor [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles -bor [Security.AccessControl.FileSystemRights]::ChangePermissions -bor [Security.AccessControl.FileSystemRights]::TakeOwnership
        if ($rule.AccessControlType -eq 'Allow' -and $rule.IdentityReference.Value -notin @('S-1-5-18','S-1-5-32-544') -and ($rule.FileSystemRights -band $writes)) { throw 'Foreign writer refused.' }
    }
}
function WriteNew([string]$Path, $Value) {
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 6))
    $pending = $Path + '.pending'
    $file = [IO.File]::Open($pending,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $file.Write($bytes,0,$bytes.Length); $file.Flush($true) } finally { $file.Dispose() }
    [IO.File]::Move($pending,$Path) # Publish only closed complete bytes, no overwrite.
}
$lock = $null
$canWriteResult = $false
try {
    if ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value -ne 'S-1-5-18' -or [Diagnostics.Process]::GetCurrentProcess().SessionId -ne 0) { throw 'SYSTEM session0 required.' }
    $data = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'Leo4\Tools'
    $binaries = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'Leo4\Tools'
    $recordPath = [IO.Path]::GetFullPath($Record).TrimEnd('\')
    $recordRoot = Join-Path $data 'operator-transition'
    if ([IO.Path]::GetDirectoryName($recordPath) -ine $recordRoot -or [IO.Path]::GetFileName($recordPath) -notmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$' -or
        [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Result)) -ine $recordPath -or [IO.Path]::GetFileName($Result) -notmatch '^L4\.OperatorBaseline\.[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\.json$') { throw 'Fixed original record/result paths required.' }
    foreach ($path in @($data,$binaries,$recordRoot,$recordPath)) { PrivatePath $path }
    if ([IO.Path]::GetPathRoot($data) -ine [IO.Path]::GetPathRoot($binaries)) { throw 'Same-volume retention required.' }
    $lockPath = Join-Path $data 'update\operations\deployment.lock'
    PrivatePath $lockPath
    $lock = [IO.File]::Open($lockPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $canWriteResult = $true
    $manifestPath = Join-Path $recordPath 'retention.json'
    $retained = Join-Path $recordPath 'retained-runtime'
        foreach ($name in @('L4Superv','L4Con','mosquitto','Leo4Proxy')) {
            if (@(Get-CimInstance Win32_Service -Filter "Name='$name'").Count) { throw 'Suite service still exists.' }
        }
        $release = Join-Path $binaries "releases\$SourceVersion"
        foreach ($process in Get-CimInstance Win32_Process) {
            if ($process.ExecutablePath -and $process.ExecutablePath.StartsWith($release+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Source release process remains; no forced kill.' }
        }
        $marker = Join-Path $data 'update\operations\update.state'
        PrivatePath $marker
        $bytes = [IO.File]::ReadAllBytes($marker)
        if ($bytes.Length -ne 112) { throw 'Invalid update marker size.' }
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $digest = $sha.ComputeHash($bytes,0,80) } finally { $sha.Dispose() }
        if ($bytes.Length -ne 112 -or [Text.Encoding]::ASCII.GetString($bytes,0,8) -cne "L4UPD01`0" -or
            [BitConverter]::ToUInt32($bytes,48) -ne 0 -or [BitConverter]::ToUInt32($bytes,52) -ne 0 -or
            [BitConverter]::ToUInt64($bytes,72) -ne 0 -or [Convert]::ToBase64String($digest) -cne [Convert]::ToBase64String($bytes[80..111])) { throw 'Active or corrupt update marker refused.' }
        $generation = [BitConverter]::ToUInt64($bytes,56)
        if (-not $generation) {
            if (@($bytes[8..47] | Where-Object { $_ -ne 0 }).Count -or [BitConverter]::ToUInt64($bytes,64)) { throw 'Invalid initial clear marker.' }
        } elseif ([Text.Encoding]::ASCII.GetString($bytes,8,36) -notmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$' -or [Guid]([Text.Encoding]::ASCII.GetString($bytes,8,36)) -eq [Guid]::Empty -or -not [BitConverter]::ToUInt64($bytes,64) -or @($bytes[44..47] | Where-Object { $_ -ne 0 }).Count) { throw 'Invalid terminal clear marker.' }
        $areas = @('config','state','logs')
        foreach ($area in $areas) {
            $path = Join-Path $data $area; RealPath $path
            $pending = [Collections.Generic.Stack[string]]::new(); $pending.Push($path); $count = 0
            while ($pending.Count) {
                foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
                    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Runtime link refused.' }
                    if (++$count -gt 10000) { throw 'Unexpected runtime inventory size.' }
                    if ($item.PSIsContainer) { $pending.Push($item.FullName) }
                }
            }
        }
        $bin = Join-Path $binaries 'bin'; RealPath $bin
        $allowed = @('leo4proxy','mosquitto','l4con','l4superv','l4desk','l4sql','l4pin','l4capture','ffmpeg') | ForEach-Object { $_+'.exe' }
        foreach ($item in Get-ChildItem -LiteralPath $bin -Force) {
            if ($item.PSIsContainer -or $item.Attributes -band [IO.FileAttributes]::ReparsePoint -or $item.Name -notin $allowed -or (Get-AuthenticodeSignature -LiteralPath $item.FullName).Status -ne 'Valid') { throw 'Unknown/unsigned launcher refused.' }
        }
        $floor = Join-Path $data 'state\catalog.floor'
        $floorHash = if (Test-Path -LiteralPath $floor) { PrivatePath $floor; (Get-FileHash -LiteralPath $floor -Algorithm SHA256).Hash } else { $null }
        if (Test-Path -LiteralPath $retained) { throw 'Existing retention refused.' }
        [IO.Directory]::CreateDirectory($retained) | Out-Null
        WriteNew $manifestPath @{source_version=$SourceVersion; floor_sha256=$floorHash; marker_sha256=(Get-FileHash -LiteralPath $marker -Algorithm SHA256).Hash}
        foreach ($area in $areas) {
            $source = [IO.Path]::GetFullPath((Join-Path $data $area)); $destination = [IO.Path]::GetFullPath((Join-Path $retained $area))
            if ([IO.Path]::GetDirectoryName($source) -ine $data -or [IO.Path]::GetDirectoryName($destination) -ine $retained) { throw 'Runtime rename escaped fixed roots.' }
            [IO.Directory]::Move($source,$destination)
            [IO.Directory]::CreateDirectory($source) | Out-Null
        }
        $savedBin = [IO.Path]::GetFullPath((Join-Path $retained 'launchers'))
        if ([IO.Path]::GetDirectoryName($bin) -ine $binaries -or [IO.Path]::GetDirectoryName($savedBin) -ine $retained) { throw 'Launcher rename escaped fixed roots.' }
        [IO.Directory]::Move($bin,$savedBin)
        [IO.Directory]::CreateDirectory($bin) | Out-Null
        [IO.File]::Move($marker,(Join-Path $retained 'update.state'))
        if ($floorHash) {
            $savedFloor = Join-Path $retained 'state\catalog.floor'
            $liveFloor = Join-Path $data 'state\catalog.floor'
            if ((Get-FileHash -LiteralPath $savedFloor -Algorithm SHA256).Hash -ine $floorHash) { throw 'Retained catalog floor differs.' }
            [IO.File]::Copy($savedFloor,$liveFloor,$false)
            Set-Acl -LiteralPath $liveFloor -AclObject (Get-Acl -LiteralPath $savedFloor)
            if ((Get-FileHash -LiteralPath $liveFloor -Algorithm SHA256).Hash -ine $floorHash) { throw 'Pre-install catalog floor readback differs.' }
        }
    WriteNew $Result @{success=$true}
    exit 0
} catch {
    if ($canWriteResult -and -not (Test-Path -LiteralPath $Result)) {
        WriteNew $Result @{success=$false;error=$_.Exception.Message}
    }
    Write-Error -ErrorRecord $_ -ErrorAction Continue
    exit 1
} finally { if ($lock) { $lock.Dispose() } }
