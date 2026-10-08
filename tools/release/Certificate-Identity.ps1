function Get-PublisherCertificateSha256 {
    param([Parameter(Mandatory = $true)][Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($algorithm.ComputeHash($Certificate.RawData))).Replace('-', '').ToLowerInvariant()
    } finally { $algorithm.Dispose() }
}
