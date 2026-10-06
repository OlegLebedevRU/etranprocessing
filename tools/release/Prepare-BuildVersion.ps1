[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Version)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*)?$') {
    throw 'Invalid explicit release version.'
}
$major = [int]$Matches[1]; $minor = [int]$Matches[2]; $patch = [int]$Matches[3]
if ($major -gt 65535 -or $minor -gt 65535 -or $patch -gt 65535) { throw 'PE version overflow.' }
$toolsRoot = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText("$toolsRoot\version.txt", "$Version`r`n", $utf8)
$common = @"
#pragma once

#define L4SETUP_VERSION_MAJOR $major
#define L4SETUP_VERSION_MINOR $minor
#define L4SETUP_VERSION_PATCH $patch
#define L4SETUP_VERSION_BUILD 0
#define L4SETUP_VERSION_STRING "$Version"
#define L4SETUP_VERSION_WSTRING L"$Version"
"@
$resource = @"
#pragma once

#define L4TOOLS_VERSION "$Version"
#define L4TOOLS_VERSION_RC $major,$minor,$patch,0

$($common.Substring($common.IndexOf('#define')))
#define VER_FILEVERSION $major,$minor,$patch,0
#define VER_PRODUCTVERSION $major,$minor,$patch,0
#define VER_FILEVERSION_STR "$Version.0\0"
#define VER_PRODUCTVERSION_STR "$Version\0"
"@
[IO.File]::WriteAllText("$toolsRoot\l4setup\src\version.h", "$common`r`n", $utf8)
[IO.File]::WriteAllText("$toolsRoot\l4setup\res\version.h", "$resource`r`n", $utf8)
$profile = $env:L4TOOLS_POLICY_BOOTSTRAP_IP
if ($profile -and $profile -notmatch '^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$') { throw 'Invalid bootstrap IPv4 profile.' }
[IO.File]::WriteAllText("$toolsRoot\l4setup\res\network-profile.bin", [string]$profile, [Text.Encoding]::ASCII)
Write-Host "Build version prepared: $Version"
