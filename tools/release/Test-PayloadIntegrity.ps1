[CmdletBinding()]
param([string]$ToolsRoot, [switch]$SkipCaptureComparison)
$ErrorActionPreference = 'Stop'
if (-not $ToolsRoot) { $ToolsRoot = (Resolve-Path "$PSScriptRoot\..").Path }
Add-Type -AssemblyName System.IO.Compression
if (-not ('L4PayloadReader' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class L4PayloadReader {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr LoadLibraryEx(string p,IntPtr h,uint f);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindResource(IntPtr h,string n,IntPtr t);
 [DllImport("kernel32.dll")] public static extern uint SizeofResource(IntPtr h,IntPtr r);
 [DllImport("kernel32.dll")] public static extern IntPtr LoadResource(IntPtr h,IntPtr r);
 [DllImport("kernel32.dll")] public static extern IntPtr LockResource(IntPtr r);
 [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr h);
 public static byte[] Read(string path,string name) {
  IntPtr h=LoadLibraryEx(path,IntPtr.Zero,2); if(h==IntPtr.Zero)throw new Exception("Cannot load installer resources");
  try {IntPtr r=FindResource(h,name,new IntPtr(10)); if(r==IntPtr.Zero)throw new Exception("Missing "+name);
   byte[] b=new byte[checked((int)SizeofResource(h,r))]; Marshal.Copy(LockResource(LoadResource(h,r)),b,0,b.Length);return b;
  } finally {FreeLibrary(h);}
 }
}
'@
}
function Stream-Hash($Stream) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Stream))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function File-Hash([string]$Path) {
    $s = [IO.File]::OpenRead($Path)
    try { return Stream-Hash $s } finally { $s.Dispose() }
}
function Program-Hash([string]$Path) {
    # Signing updates checksum/security directory and appends a certificate.
    # Compare every PE section (code, data, resources) against the fresh build.
    $bytes = [IO.File]::ReadAllBytes($Path)
    $pe = [BitConverter]::ToInt32($bytes, 60)
    $count = [BitConverter]::ToUInt16($bytes, $pe+6)
    $section = $pe+24+[BitConverter]::ToUInt16($bytes, $pe+20)
    $stream = New-Object IO.MemoryStream
    try {
        for ($i=0; $i -lt $count; $i++) {
            $length = [BitConverter]::ToInt32($bytes, $section+16)
            $offset = [BitConverter]::ToInt32($bytes, $section+20)
            if ($length -lt 0 -or $offset -lt 0 -or [long]$offset+$length -gt $bytes.Length) { throw 'Invalid PE section.' }
            $stream.Write($bytes, $offset, $length)
            $section += 40
        }
        $stream.Position=0
        return Stream-Hash $stream
    } finally { $stream.Dispose() }
}
$setup = (Resolve-Path -LiteralPath "$ToolsRoot\dist\l4setup.exe").Path
$release = Get-Content -LiteralPath "$ToolsRoot\dist\l4tools-release.json" -Raw | ConvertFrom-Json
if ($release.version -ne (Get-Content -LiteralPath "$ToolsRoot\version.txt" -Raw).Trim() -or
    (File-Hash $setup) -ne $release.files.'l4setup.exe'.sha256) { throw 'Installer/version manifest mismatch.' }
foreach ($arch in @('x86','x64')) {
    $bytes = [L4PayloadReader]::Read($setup, "PAYLOAD_$($arch.ToUpperInvariant())")
    $memory = New-Object IO.MemoryStream(,$bytes)
    $zip = $null
    try {
        if ((Stream-Hash $memory) -ne $release.payload_sha256.$arch) { throw "Embedded payload hash mismatch: $arch" }
        $memory.Position = 0
        $zip = New-Object IO.Compression.ZipArchive($memory, [IO.Compression.ZipArchiveMode]::Read)
        $stage = (Resolve-Path -LiteralPath "$ToolsRoot\dist\.stage\$arch").Path
        $files = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
        $entries = @($zip.Entries | Where-Object { $_.Name })
        if ($files.Count -ne $entries.Count) { throw "Payload file count mismatch: $arch" }
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($stage.Length+1).Replace('\','/')
            $entry = $zip.GetEntry($relative)
            if (-not $entry) { $entry = $zip.GetEntry($relative.Replace('/','\')) }
            if (-not $entry -or $entry.Length -ne $file.Length) { throw "Payload missing/wrong size: $relative" }
            $stream = $entry.Open()
            try { if ((Stream-Hash $stream) -ne (File-Hash $file.FullName)) { throw "Stale payload file: $relative" } }
            finally { $stream.Dispose() }
        }
        if (-not $SkipCaptureComparison -and (Program-Hash "$stage\l4capture\bin\l4capture.exe") -ne
            (Program-Hash "$ToolsRoot\l4capture\bin\$arch\l4capture.exe")) { throw "Stale l4capture in $arch staging" }
        Write-Host "PASS ${arch}: $($files.Count) embedded files match staging (capture source comparison skipped=$SkipCaptureComparison)"
    } finally { if ($zip) { $zip.Dispose() }; $memory.Dispose() }
}
