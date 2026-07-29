using System.Configuration;



/// <summary>
/// Менеджер конфигурации службы.
/// </summary>
public sealed class EtranConfigurationManager
{

    static public string EtranConfig
    {
        get
        {
            return ConfigurationManager.AppSettings["EtranConfig"];
        }
    }

    static public string DBConnName
    {
        get
        {
            return ConfigurationManager.AppSettings["DBConnName"];
        }
    }

}
