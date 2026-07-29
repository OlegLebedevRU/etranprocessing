using System.Configuration;

/// <summary>
/// Менеджер конфигурации службы.
/// </summary>
public sealed class EtranConfigurationManager
{

    static public string UrlPay
    {
        get { return ConfigurationManager.AppSettings["UrlPay"]; }
    }
    static public string Token
    {
        get
        {
            return ConfigurationManager.AppSettings["Token"];
        }
    }
    static public string Login
    {
        get
        {
            return ConfigurationManager.AppSettings["Login"];
        }
    }
    static public string Pwd
    {
        get
        {
            return ConfigurationManager.AppSettings["Pwd"];
        }
    }

}
