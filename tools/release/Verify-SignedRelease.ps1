[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$PfxPath
)
$ErrorActionPreference = 'Stop'
$toolsRoot = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$dist = "$toolsRoot\dist"
$certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
    $PfxPath, $env:L4TOOLS_SIGN_PFX_PASSWORD,
    [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::DefaultKeySet)
if (-not $certificate.HasPrivateKey) { throw 'Signing certificate has no private key.' }
try {
    $manifest = Get-Content -LiteralPath "$dist\l4tools-release.json" -Raw | ConvertFrom-Json
    if ($manifest.version -ne $Version -or -not $manifest.signed) { throw 'Signed manifest/version mismatch.' }
    $files = @()
    foreach ($arch in @('x86', 'x64')) {
        $stage = "$dist\.stage\$arch"
        if (-not (Test-Path -LiteralPath $stage -PathType Container)) { throw "Missing stage: $arch" }
        $archFiles = @(Get-ChildItem -LiteralPath $stage -Recurse -File -Filter '*.exe')
        if ($archFiles.Count -eq 0) { throw "Empty stage: $arch" }
        $files += $archFiles
    }
    $files += Get-Item -LiteralPath "$dist\l4setup.exe"
    foreach ($file in $files) {
        $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
        if ($signature.Status -ne 'Valid' -or -not $signature.TimeStamperCertificate -or
            -not $signature.SignerCertificate -or
            $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
            throw "Invalid timestamped expected-publisher signature: $($file.FullName)"
        }
    }
    if ((Get-Item -LiteralPath "$dist\l4setup.exe").VersionInfo.ProductVersion.Trim([char]0).Trim() -ne $Version) {
        throw 'Setup PE version mismatch.'
    }
    & "$PSScriptRoot\Test-PayloadIntegrity.ps1" -ToolsRoot $toolsRoot
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'Embedded payload verification failed.' }
    Write-Host "Verified $($files.Count) signed EXEs and both embedded payloads."
} finally { $certificate.Dispose() }
