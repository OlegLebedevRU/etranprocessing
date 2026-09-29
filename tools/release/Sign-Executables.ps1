[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$TargetPath
)

$ErrorActionPreference = 'Stop'
$pfxPath = $env:L4TOOLS_SIGN_PFX
$password = $env:L4TOOLS_SIGN_PFX_PASSWORD
if (-not $pfxPath -or -not (Test-Path -LiteralPath $pfxPath -PathType Leaf)) {
    throw 'L4TOOLS_SIGN_PFX must name an existing PFX file.'
}
$signtool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe'
if (-not (Test-Path -LiteralPath $signtool -PathType Leaf)) {
    throw 'Windows SDK signtool.exe is unavailable.'
}
$flags = [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::DefaultKeySet
$certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($pfxPath, $password, $flags)
if (-not $certificate.HasPrivateKey) { throw 'Signing PFX has no private key.' }
$expectedThumbprint = $certificate.Thumbprint

$resolved = (Resolve-Path -LiteralPath $TargetPath).Path
if (Test-Path -LiteralPath $resolved -PathType Container) {
    $executables = @(Get-ChildItem -LiteralPath $resolved -Recurse -File -Filter '*.exe')
} else {
    if ([IO.Path]::GetExtension($resolved) -ne '.exe') { throw 'Target must be an EXE or a directory.' }
    $executables = @(Get-Item -LiteralPath $resolved)
}
if ($executables.Count -eq 0) { throw 'No executable files to sign.' }
foreach ($exe in $executables) {
    # The password is read from the environment and is never printed or stored in a tracked file.
    if ($password) {
        $signOutput = & $signtool sign /fd SHA256 /f $pfxPath /p $password $exe.FullName 2>&1
    } else {
        $signOutput = & $signtool sign /fd SHA256 /f $pfxPath $exe.FullName 2>&1
    }
    if ($LASTEXITCODE -ne 0) { throw "Signing failed for $($exe.FullName): $($signOutput -join ' ')" }
    $signature = Get-AuthenticodeSignature -LiteralPath $exe.FullName
    if (-not $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $expectedThumbprint -or
        $signature.Status -notin @('Valid', 'NotTrusted')) {
        throw "Signature verification failed for $($exe.FullName): $($signature.Status)"
    }
    Write-Host "Signed: $($exe.FullName) (status: $($signature.Status))"
}
Write-Host "Signed $($executables.Count) executable(s)."
