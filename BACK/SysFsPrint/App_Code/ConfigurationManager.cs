using System;
using System.Configuration;



/// <summary>
/// Менеджер конфигурации службы.
/// </summary>
public sealed class EtranConfigurationManager
{

    static public string Url
    {
        get
        {
            return ConfigurationManager.AppSettings["Url"];
        }
    }

    static public string OperCodes
    {
        get
        {
            return ConfigurationManager.AppSettings["OperCodes"];
        }
    }
}
