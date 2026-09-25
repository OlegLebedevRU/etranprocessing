param(
    [string]$StageRoot,
    [string]$ArchivePath
)

$ErrorActionPreference = 'Stop'

function Get-Sha256Hex([string]$Path) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        return ([System.BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $stream.Dispose()
        $algorithm.Dispose()
    }
}

try {
    if ($StageRoot) {
        $root = [System.IO.Path]::GetFullPath($StageRoot).TrimEnd('\')
        $lines = @(
            Get-ChildItem -LiteralPath $root -Recurse -File |
                Where-Object { $_.Name -ne 'ffmpeg.sha256' } |
                Sort-Object FullName |
                ForEach-Object {
                    '{0}  {1}' -f (Get-Sha256Hex $_.FullName),
                        ($_.FullName.Substring($root.Length + 1).Replace('\', '/'))
                }
        )
        if ($lines.Count -eq 0) { throw 'No files found for FFmpeg manifest' }
        [System.IO.File]::WriteAllLines((Join-Path $root 'ffmpeg.sha256'), $lines,
            [System.Text.Encoding]::ASCII)
    }
    elseif ($ArchivePath) {
        $archive = [System.IO.Path]::GetFullPath($ArchivePath)
        if (-not [System.IO.File]::Exists($archive)) { throw "Archive missing: $archive" }
        $line = '{0}  ffmpeg.zip' -f (Get-Sha256Hex $archive)
        [System.IO.File]::WriteAllText(($archive + '.sha256'), ($line + [Environment]::NewLine),
            [System.Text.Encoding]::ASCII)
    }
    else { throw 'Specify StageRoot or ArchivePath' }
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
