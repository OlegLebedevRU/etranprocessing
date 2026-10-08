[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$TargetPath,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-f]{64}$')][string]$PublisherSha256
)
$ErrorActionPreference = 'Stop'
$signature = Get-AuthenticodeSignature -LiteralPath $TargetPath
if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or
    -not $signature.TimeStamperCertificate) { throw 'Bootstrap signature/timestamp is invalid.' }
$hash = [Security.Cryptography.SHA256]::Create()
try {
    $actual = ([BitConverter]::ToString($hash.ComputeHash($signature.SignerCertificate.RawData))).Replace('-', '').ToLowerInvariant()
} finally { $hash.Dispose() }
if ($actual -ne $PublisherSha256) { throw 'Bootstrap publisher differs from signed release.' }
$tool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe'
& $tool verify /pa /all /tw $TargetPath
if ($LASTEXITCODE -ne 0) { throw 'Bootstrap Authenticode verification failed.' }
