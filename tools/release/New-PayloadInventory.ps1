[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Stage,
    [Parameter(Mandatory = $true)][ValidateSet('x86','x64')][string]$Arch,
    [Parameter(Mandatory = $true)][string]$Version
)
$ErrorActionPreference = 'Stop'
function Get-PayloadHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($hasher.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally { $stream.Dispose(); $hasher.Dispose() }
}
$paths = @('leo4proxy\leo4proxy.exe', 'mosquitto\mosquitto.exe',
    'l4con\l4con.exe', 'l4superv\l4superv.exe', 'l4pin\l4pin.exe',
    'l4desk\l4desk.exe', 'l4capture\bin\l4capture.exe', 'ffmpeg\ffmpeg.exe', 'l4sql\l4sql.exe')
$entries = foreach ($relative in $paths) {
    $file = Get-Item -LiteralPath (Join-Path $Stage $relative)
    $productVersion = $file.VersionInfo.ProductVersion
    if (-not $productVersion -and $relative -notin @('mosquitto\mosquitto.exe','ffmpeg\ffmpeg.exe')) {
        throw "Missing first-party PE version: $($file.FullName)"
    }
    [ordered]@{
        path = $relative
        product_version = if ($productVersion) { $productVersion.Trim([char]0).Trim() } else { $null }
        sha256 = Get-PayloadHash $file.FullName
        size = $file.Length
    }
}
$inventory = [ordered]@{ schema = 1; version = $Version; arch = $Arch; components = @($entries) }
[IO.File]::WriteAllText((Join-Path $Stage 'l4superv\package-components.json'),
    ($inventory | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
