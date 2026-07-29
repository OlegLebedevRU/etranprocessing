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

    static public int WaitResult
    {
        get
        {
            return int.Parse(ConfigurationManager.AppSettings["WaitResult"]);
        }
    }

}
