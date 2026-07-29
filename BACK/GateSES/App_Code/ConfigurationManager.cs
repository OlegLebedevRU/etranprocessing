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

    static public string DbConnName
    {
        get
        {
            return ConfigurationManager.AppSettings["DBConnName"];
        }
    }

    static public string DbConnPlaterra
    {
        get
        {
            return GlobalObjectsManager.DbConnectionString;
        }
    }

}
