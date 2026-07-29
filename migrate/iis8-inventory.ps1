[CmdletBinding()]
param(
    [string]$OutputRoot = "",
    [switch]$IncludeConfigBackups,
    [switch]$IncludeWebConfigScan
)

$ErrorActionPreference = "Stop"

function New-OutputDirectory {
    param([string]$BasePath)

    if ([string]::IsNullOrWhiteSpace($BasePath)) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $BasePath = Join-Path $env:SystemDrive ("iis-audit-" + $stamp)
    }

    if (-not (Test-Path -LiteralPath $BasePath)) {
        New-Item -ItemType Directory -Path $BasePath | Out-Null
    }

    return (Resolve-Path $BasePath).Path
}

function Safe-GetObjectValue {
    param(
        $Object,
        [string]$PropertyName
    )

    try {
        if ($null -eq $Object) {
            return $null
        }
        return $Object.$PropertyName
    }
    catch {
        return $null
    }
}

function Safe-GetItemPropertyValue {
    param(
        [string]$Path,
        [string]$Name
    )

    try {
        return (Get-ItemProperty -Path $Path -Name $Name -ErrorAction Stop).$Name
    }
    catch {
        return $null
    }
}

function Convert-BindingInfo {
    param($Binding)

    $bindingInfo = Safe-GetObjectValue $Binding "bindingInformation"
    $protocol = Safe-GetObjectValue $Binding "protocol"
    $certificateStoreName = Safe-GetObjectValue $Binding "certificateStoreName"
    $sslFlags = Safe-GetObjectValue $Binding "sslFlags"

    $ipAddress = $null
    $port = $null
    $hostHeader = $null

    if (-not [string]::IsNullOrWhiteSpace($bindingInfo)) {
        $parts = $bindingInfo -split ":", 3
        if ($parts.Count -ge 1) { $ipAddress = $parts[0] }
        if ($parts.Count -ge 2) { $port = $parts[1] }
        if ($parts.Count -ge 3) { $hostHeader = $parts[2] }
    }

    $certificateHash = $null
    try {
        if ($Binding.certificateHash) {
            $certificateHash = ([System.BitConverter]::ToString($Binding.certificateHash)).Replace("-", "")
        }
    }
    catch {
        $certificateHash = $null
    }

    return [pscustomobject]@{
        Protocol           = $protocol
        BindingInformation = $bindingInfo
        IPAddress          = $ipAddress
        Port               = $port
        HostHeader         = $hostHeader
        CertificateHash    = $certificateHash
        CertificateStore   = $certificateStoreName
        SslFlags           = $sslFlags
    }
}

function Get-CertificateInventory {
    $stores = @(
        "Cert:\LocalMachine\My",
        "Cert:\LocalMachine\WebHosting",
        "Cert:\LocalMachine\CA",
        "Cert:\LocalMachine\Root"
    )

    $result = @()

    foreach ($store in $stores) {
        if (Test-Path $store) {
            $items = Get-ChildItem -Path $store -ErrorAction SilentlyContinue
            foreach ($item in $items) {
                $san = $null
                $signatureAlgorithm = $null
                $publicKeyAlgorithm = $null

                try {
                    $sanExt = $item.Extensions | Where-Object { $_.Oid.FriendlyName -eq "Subject Alternative Name" }
                    if ($sanExt) {
                        $san = $sanExt.Format($true)
                    }
                }
                catch {}

                try {
                    if ($item.SignatureAlgorithm) {
                        $signatureAlgorithm = $item.SignatureAlgorithm.FriendlyName
                    }
                }
                catch {}

                try {
                    if ($item.PublicKey -and $item.PublicKey.Oid) {
                        $publicKeyAlgorithm = $item.PublicKey.Oid.FriendlyName
                    }
                }
                catch {}

                $result += [pscustomobject]@{
                    StorePath          = $store
                    Subject            = $item.Subject
                    Issuer             = $item.Issuer
                    Thumbprint         = $item.Thumbprint
                    FriendlyName       = $item.FriendlyName
                    NotBefore          = $item.NotBefore
                    NotAfter           = $item.NotAfter
                    HasPrivateKey      = $item.HasPrivateKey
                    SignatureAlgorithm = $signatureAlgorithm
                    PublicKeyAlgorithm = $publicKeyAlgorithm
                    SerialNumber       = $item.SerialNumber
                    SubjectAltName     = $san
                }
            }
        }
    }

    return $result
}

function Get-WebConfigFiles {
    param(
        [string[]]$Roots
    )

    $files = @()

    foreach ($root in $Roots) {
        if (-not [string]::IsNullOrWhiteSpace($root) -and (Test-Path $root)) {
            try {
                $found = Get-ChildItem -Path $root -Filter web.config -File -Recurse -ErrorAction SilentlyContinue
                foreach ($file in $found) {
                    $files += [pscustomobject]@{
                        FullName      = $file.FullName
                        DirectoryName = $file.DirectoryName
                        Length        = $file.Length
                        LastWriteTime = $file.LastWriteTime
                    }
                }
            }
            catch {}
        }
    }

    return $files | Sort-Object FullName -Unique
}

function Get-SitePhysicalPath {
    param([string]$SiteName)

    try {
        $siteItem = Get-Item ("IIS:\Sites\" + $SiteName)
        return $siteItem.physicalPath
    }
    catch {
        return $null
    }
}

function Get-WebConfigPropertyValue {
    param(
        [string]$PSPath,
        [string]$Filter,
        [string]$Name
    )

    try {
        $value = Get-WebConfigurationProperty -PSPath $PSPath -Filter $Filter -Name $Name -ErrorAction SilentlyContinue
        if ($null -ne $value) {
            return $value.Value
        }
        return $null
    }
    catch {
        return $null
    }
}

function Add-Note {
    param(
        [ref]$ReportRef,
        [string]$Message
    )

    $ReportRef.Value.Notes += $Message
}

Import-Module WebAdministration

$outputDir = New-OutputDirectory -BasePath $OutputRoot

$report = [ordered]@{
    ComputerName          = $env:COMPUTERNAME
    CollectedAt           = (Get-Date).ToString("s")
    IISVersion            = $null
    OSVersion             = $null
    Features              = @()
    Sites                 = @()
    Applications          = @()
    VirtualDirectories    = @()
    AppPools              = @()
    Certificates          = @()
    UrlRewriteGlobalRules = @()
    RequestFiltering      = @()
    HttpRedirects         = @()
    Authorization         = @()
    ArrProxy              = @()
    WebConfigFiles        = @()
    Notes                 = @()
}

try {
    $os = Get-CimInstance Win32_OperatingSystem
    $report.OSVersion = [pscustomobject]@{
        Caption     = $os.Caption
        Version     = $os.Version
        BuildNumber = $os.BuildNumber
    }
}
catch {
    Add-Note -ReportRef ([ref]$report) -Message ("Failed to read OS version: " + $_.Exception.Message)
}

try {
    $iisKey = "HKLM:\SOFTWARE\Microsoft\InetStp"
    $report.IISVersion = [pscustomobject]@{
        VersionString = Safe-GetItemPropertyValue -Path $iisKey -Name "VersionString"
        MajorVersion  = Safe-GetItemPropertyValue -Path $iisKey -Name "MajorVersion"
        MinorVersion  = Safe-GetItemPropertyValue -Path $iisKey -Name "MinorVersion"
    }
}
catch {
    Add-Note -ReportRef ([ref]$report) -Message ("Failed to read IIS version: " + $_.Exception.Message)
}

try {
    $featureNames = @(
        "Web-Server",
        "Web-WebServer",
        "Web-Common-Http",
        "Web-Default-Doc",
        "Web-Static-Content",
        "Web-Http-Redirect",
        "Web-Health",
        "Web-Http-Logging",
        "Web-Performance",
        "Web-Security",
        "Web-Filtering",
        "Web-Basic-Auth",
        "Web-Windows-Auth",
        "Web-Client-Auth",
        "Web-Digest-Auth",
        "Web-CertProvider",
        "Web-Url-Auth",
        "Web-IP-Security",
        "Web-App-Dev",
        "Web-CGI",
        "Web-ISAPI-Ext",
        "Web-ISAPI-Filter",
        "Web-Mgmt-Tools",
        "Web-Mgmt-Console"
    )

    try {
        $report.Features = Get-WindowsFeature |
            Where-Object { $featureNames -contains $_.Name } |
            Select-Object Name, DisplayName, InstallState
    }
    catch {
        Add-Note -ReportRef ([ref]$report) -Message ("Get-WindowsFeature not available or failed: " + $_.Exception.Message)
    }
}
catch {
    Add-Note -ReportRef ([ref]$report) -Message ("Failed to enumerate Windows features: " + $_.Exception.Message)
}

try {
    $sites = Get-Website
    foreach ($site in $sites) {
        $siteBindings = @()
        foreach ($binding in $site.Bindings.Collection) {
            $siteBindings += (Convert-BindingInfo -Binding $binding)
        }

        $sitePath = Get-SitePhysicalPath -SiteName $site.Name

        $applicationPool = Safe-GetObjectValue $site "applicationPool"
        $logFileDirectory = $null
        $logFormat = $null

        try {
            if ($site.logFile) {
                $logFileDirectory = Safe-GetObjectValue $site.logFile "directory"
                $logFormat = Safe-GetObjectValue $site.logFile "logFormat"
            }
        }
        catch {}

        $siteItem = [pscustomobject]@{
            Name             = $site.Name
            Id               = $site.Id
            State            = $site.State
            PhysicalPath     = $sitePath
            ApplicationPool  = $applicationPool
            Bindings         = $siteBindings
            LogFileDirectory = $logFileDirectory
            LogFormat        = $logFormat
        }

        $report.Sites += $siteItem

        try {
            $location = "IIS:\Sites\" + $site.Name
            $anon = Get-WebConfigPropertyValue -PSPath $location -Filter "system.webServer/security/authentication/anonymousAuthentication" -Name "enabled"
            $basic = Get-WebConfigPropertyValue -PSPath $location -Filter "system.webServer/security/authentication/basicAuthentication" -Name "enabled"
            $windows = Get-WebConfigPropertyValue -PSPath $location -Filter "system.webServer/security/authentication/windowsAuthentication" -Name "enabled"
            $clientCert = Get-WebConfigPropertyValue -PSPath $location -Filter "system.webServer/security/access" -Name "sslFlags"

            $report.Authorization += [pscustomobject]@{
                Scope                   = $site.Name
                AnonymousAuthentication = $anon
                BasicAuthentication     = $basic
                WindowsAuthentication   = $windows
                SslFlags                = $clientCert
            }
        }
        catch {
            Add-Note -ReportRef ([ref]$report) -Message ("Failed auth read for site [" + $site.Name + "]: " + $_.Exception.Message)
        }

        try {
            $rf = Get-WebConfiguration -PSPath ("IIS:\Sites\" + $site.Name) -Filter "system.webServer/security/requestFiltering" -ErrorAction SilentlyContinue
            if ($rf) {
                $allowDoubleEscaping = Safe-GetObjectValue $rf "allowDoubleEscaping"
                $allowHighBitCharacters = Safe-GetObjectValue $rf "allowHighBitCharacters"
                $removeServerHeader = Safe-GetObjectValue $rf "removeServerHeader"
                $maxAllowedContentLength = $null
                $maxUrl = $null
                $maxQueryString = $null

                try {
                    if ($rf.requestLimits) {
                        $maxAllowedContentLength = Safe-GetObjectValue $rf.requestLimits "maxAllowedContentLength"
                        $maxUrl = Safe-GetObjectValue $rf.requestLimits "maxUrl"
                        $maxQueryString = Safe-GetObjectValue $rf.requestLimits "maxQueryString"
                    }
                }
                catch {}

                $report.RequestFiltering += [pscustomobject]@{
                    Scope                   = $site.Name
                    AllowDoubleEscaping     = $allowDoubleEscaping
                    AllowHighBitCharacters  = $allowHighBitCharacters
                    RemoveServerHeader      = $removeServerHeader
                    MaxAllowedContentLength = $maxAllowedContentLength
                    MaxUrl                  = $maxUrl
                    MaxQueryString          = $maxQueryString
                }
            }
        }
        catch {
            Add-Note -ReportRef ([ref]$report) -Message ("Failed requestFiltering read for site [" + $site.Name + "]: " + $_.Exception.Message)
        }

        try {
            $redir = Get-WebConfiguration -PSPath ("IIS:\Sites\" + $site.Name) -Filter "system.webServer/httpRedirect" -ErrorAction SilentlyContinue
            if ($redir) {
                $report.HttpRedirects += [pscustomobject]@{
                    Scope              = $site.Name
                    Enabled            = Safe-GetObjectValue $redir "enabled"
                    Destination        = Safe-GetObjectValue $redir "destination"
                    ExactDestination   = Safe-GetObjectValue $redir "exactDestination"
                    HttpResponseStatus = Safe-GetObjectValue $redir "httpResponseStatus"
                    ChildOnly          = Safe-GetObjectValue $redir "childOnly"
                }
            }
        }
        catch {
            Add-Note -ReportRef ([ref]$report) -Message ("Failed httpRedirect read for site [" + $site.Name + "]: " + $_.Exception.Message)
        }
    }
}
catch {
    Add-Note -ReportRef ([ref]$report) -Message ("Failed to enumerate IIS sites: " + $_.Exception.Message)
}

try {
    $apps = Get-WebApplication
    foreach ($app in $apps) {
        $siteName = $null
        try {
            if ($app.ItemXPath -match "site\[@name='([^']+)'\]") {
                $siteName = $matches[1]
            }
        }
        catch {}

        $preloadEnabled = Safe-GetObjectValue $app "preloadEnabled"

        $report.Applications += [pscustomobject]@{
            Site             = $siteName
            Path             = $app.Path
            PhysicalPath     = $app.PhysicalPath
            ApplicationPool  = $app.ApplicationPool
            EnabledProtocols = $app.EnabledProtocols
            PreloadEnabled   = $preloadEnabled
        }
    }
}
catch {
    Add-Note -ReportRef ([ref]$report) -Message ("Failed to enumerate applications: " + $_.Exception.Message)
}

try {
    $vdirs = Get-WebVirtualDirectory
    foreach ($vdir in $vdirs) {
        $siteName = $null
        try {
            if ($vdir.ItemXPath -match "site\[@name='([^']+)'\]") {
                $siteName = $matches[1]
            }
        }
        catch {}

        $report.VirtualDirectories += [pscustomobject]@{
            Site         = $siteName
            Path         = $vdir.Path
            PhysicalPath = $vdir.PhysicalPath
        }
    }
}
catch {
    Add-Note -ReportRef ([ref]$report) -Message ("Failed to enumerate virtual directories: " + $_.Exception.Message)
}

try {
    $appPools = Get-ChildItem IIS:\AppPools
    foreach ($pool in $appPools) {
        $idleTimeoutMinutes = $null
        $recyclingPeriodicMins = $null
        $processModelIdentity = $null
        $maxProcesses = $null
        $loadUserProfile = $null

        try {
            if ($pool.processModel) {
                $processModelIdentity = Safe-GetObjectValue $pool.processModel "identityType"
                $maxProcesses = Safe-GetObjectValue $pool.processModel "maxProcesses"
                $loadUserProfile = Safe-GetObjectValue $pool.processModel "loadUserProfile"

                if ($pool.processModel.idleTimeout) {
                    $idleTimeoutMinutes = $pool.processModel.idleTimeout.TotalMinutes
                }
            }
        }
        catch {}

        try {
            if ($pool.recycling -and $pool.recycling.periodicRestart -and $pool.recycling.periodicRestart.time) {
                $recyclingPeriodicMins = $pool.recycling.periodicRestart.time.TotalMinutes
            }
        }
        catch {}

        $report.AppPools += [pscustomobject]@{
            Name                  = $pool.Name
            State                 = $pool.State
            ManagedRuntimeVersion = $pool.managedRuntimeVersion
            ManagedPipelineMode   = $pool.managedPipelineMode
            AutoStart             = $pool.autoStart
            StartMode             = Safe-GetObjectValue $pool "startMode"
            QueueLength           = Safe-GetObjectValue $pool "queueLength"
            Enable32BitAppOnWin64 = Safe-GetObjectValue $pool "enable32BitAppOnWin64"
            ProcessModelIdentity  = $processModelIdentity
            IdleTimeoutMinutes    = $idleTimeoutMinutes
            MaxProcesses          = $maxProcesses
            LoadUserProfile       = $loadUserProfile
            RecyclingPeriodicMins = $recyclingPeriodicMins
        }
    }
}
catch {
    Add-Note -ReportRef ([ref]$report) -Message ("Failed to enumerate app pools: " + $_.Exception.Message)
}

try {
    $report.Certificates = Get-CertificateInventory
}
catch {
    Add-Note -ReportRef ([ref]$report) -Message ("Failed to enumerate certificates: " + $_.Exception.Message)
}

try {
    $globalRewrite = Get-WebConfiguration -PSPath "MACHINE/WEBROOT/APPHOST" -Filter "system.webServer/rewrite/rules/rule" -ErrorAction SilentlyContinue
    foreach ($rule in $globalRewrite) {
        $matchUrl = $null
        $actionType = $null
        $actionUrl = $null
        $redirectType = $null
        $appendQuery = $null

        try {
            if ($rule.match) {
                $matchUrl = Safe-GetObjectValue $rule.match "url"
            }
        }
        catch {}

        try {
            if ($rule.action) {
                $actionType = Safe-GetObjectValue $rule.action "type"
                $actionUrl = Safe-GetObjectValue $rule.action "url"
                $redirectType = Safe-GetObjectValue $rule.action "redirectType"
                $appendQuery = Safe-GetObjectValue $rule.action "appendQueryString"
            }
        }
        catch {}

        $report.UrlRewriteGlobalRules += [pscustomobject]@{
            Scope          = "GLOBAL"
            Name           = Safe-GetObjectValue $rule "name"
            PatternSyntax  = Safe-GetObjectValue $rule "patternSyntax"
            StopProcessing = Safe-GetObjectValue $rule "stopProcessing"
            MatchUrl       = $matchUrl
            ActionType     = $actionType
            ActionUrl      = $actionUrl
            RedirectType   = $redirectType
            AppendQuery    = $appendQuery
        }
    }
}
catch {
    Add-Note -ReportRef ([ref]$report) -Message ("Failed to read global rewrite rules: " + $_.Exception.Message)
}

try {
    $arrRoot = "MACHINE/WEBROOT/APPHOST"
    $report.ArrProxy += [pscustomobject]@{
        Scope                             = "GLOBAL"
        Enabled                           = Get-WebConfigPropertyValue -PSPath $arrRoot -Filter "system.webServer/proxy" -Name "enabled"
        PreserveHostHeader                = Get-WebConfigPropertyValue -PSPath $arrRoot -Filter "system.webServer/proxy" -Name "preserveHostHeader"
        ReverseRewriteHostInResponseHeaders = Get-WebConfigPropertyValue -PSPath $arrRoot -Filter "system.webServer/proxy" -Name "reverseRewriteHostInResponseHeaders"
    }
}
catch {
    Add-Note -ReportRef ([ref]$report) -Message ("Failed to read ARR proxy settings: " + $_.Exception.Message)
}

if ($IncludeWebConfigScan) {
    try {
        $roots = @()
        foreach ($siteEntry in $report.Sites) {
            if (-not [string]::IsNullOrWhiteSpace($siteEntry.PhysicalPath)) {
                $roots += $siteEntry.PhysicalPath
            }
        }

        $report.WebConfigFiles = Get-WebConfigFiles -Roots $roots
    }
    catch {
        Add-Note -ReportRef ([ref]$report) -Message ("Failed web.config scan: " + $_.Exception.Message)
    }
}

try {
    $jsonPath = Join-Path $outputDir "iis-inventory.json"
    $report | ConvertTo-Json -Depth 8 | Out-File -FilePath $jsonPath -Encoding UTF8
}
catch {
    Write-Warning ("Failed to write JSON: " + $_.Exception.Message)
}

try {
    $report.Sites |
        Select-Object Name, Id, State, PhysicalPath, ApplicationPool |
        Export-Csv -NoTypeInformation -Encoding UTF8 -Path (Join-Path $outputDir "sites.csv")
}
catch {}

try {
    $bindingRows = @()
    foreach ($siteEntry in $report.Sites) {
        foreach ($binding in $siteEntry.Bindings) {
            $bindingRows += [pscustomobject]@{
                SiteName           = $siteEntry.Name
                Protocol           = $binding.Protocol
                BindingInformation = $binding.BindingInformation
                IPAddress          = $binding.IPAddress
                Port               = $binding.Port
                HostHeader         = $binding.HostHeader
                CertificateHash    = $binding.CertificateHash
                CertificateStore   = $binding.CertificateStore
                SslFlags           = $binding.SslFlags
            }
        }
    }

    $bindingRows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path (Join-Path $outputDir "bindings.csv")
}
catch {}

try {
    $report.AppPools |
        Export-Csv -NoTypeInformation -Encoding UTF8 -Path (Join-Path $outputDir "apppools.csv")
}
catch {}

try {
    $report.Certificates |
        Export-Csv -NoTypeInformation -Encoding UTF8 -Path (Join-Path $outputDir "certificates.csv")
}
catch {}

try {
    $summary = @()
    $summary += "IIS inventory summary"
    $summary += ("ComputerName: " + $report.ComputerName)
    $summary += ("CollectedAt: " + $report.CollectedAt)
    $summary += ("Sites: " + $report.Sites.Count)
    $summary += ("Applications: " + $report.Applications.Count)
    $summary += ("VirtualDirectories: " + $report.VirtualDirectories.Count)
    $summary += ("AppPools: " + $report.AppPools.Count)
    $summary += ("Certificates: " + $report.Certificates.Count)
    $summary += ("RewriteRules(Global): " + $report.UrlRewriteGlobalRules.Count)
    $summary += ("RequestFiltering entries: " + $report.RequestFiltering.Count)
    $summary += ("HttpRedirect entries: " + $report.HttpRedirects.Count)
    $summary += ("Authorization entries: " + $report.Authorization.Count)
    $summary += ("ARR entries: " + $report.ArrProxy.Count)

    if ($IncludeWebConfigScan) {
        $summary += ("web.config files found: " + $report.WebConfigFiles.Count)
    }

    if ($report.Notes.Count -gt 0) {
        $summary += ""
        $summary += "Notes:"
        foreach ($note in $report.Notes) {
            $summary += $note
        }
    }

    $summary | Out-File -FilePath (Join-Path $outputDir "summary.txt") -Encoding UTF8
}
catch {}

if ($IncludeConfigBackups) {
    try {
        $backupDir = Join-Path $outputDir "config-backup"
        New-Item -ItemType Directory -Path $backupDir -Force | Out-Null

        $appHost = Join-Path $env:windir "System32\inetsrv\config\applicationHost.config"
        if (Test-Path $appHost) {
            Copy-Item -LiteralPath $appHost -Destination (Join-Path $backupDir "applicationHost.config") -Force
        }

        $adminConfig = Join-Path $env:windir "System32\inetsrv\config\administration.config"
        if (Test-Path $adminConfig) {
            Copy-Item -LiteralPath $adminConfig -Destination (Join-Path $backupDir "administration.config") -Force
        }
    }
    catch {
        Add-Note -ReportRef ([ref]$report) -Message ("Failed config backup: " + $_.Exception.Message)
    }
}

Write-Host ""
Write-Host "IIS inventory completed." -ForegroundColor Green
Write-Host ("Output directory: " + $outputDir)
Write-Host ("Main report: " + (Join-Path $outputDir "iis-inventory.json"))
Write-Host ""