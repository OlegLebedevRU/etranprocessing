using System.Configuration;

/// <summary>
/// Менеджер конфигурации службы.
/// </summary>
public sealed class EtranConfigurationManager
{
    static public string EtranConfig
    {
        get { return ConfigurationManager.AppSettings["EtranConfig"]; }
    }

    static public string UrlCheck
    {
        get { return ConfigurationManager.AppSettings["UrlCheck"]; }
    }

    static public string UrlCheck2
    {
        get { return ConfigurationManager.AppSettings["UrlCheck2"]; }
    }

    static public string UrlPay
    {
        get { return ConfigurationManager.AppSettings["UrlPay"]; }
    }
}
