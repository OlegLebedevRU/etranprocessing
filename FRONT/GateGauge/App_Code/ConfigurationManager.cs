using System.Configuration;

public sealed class EtranConfigurationManager
{
    public static string SendScriptsToKioskUrl
    {
        get
        {
            return ConfigurationManager.AppSettings["SendScriptsToKioskUrl"];
        }
    }
    public static string EtranConfig
    {
        get
        {
            return ConfigurationManager.AppSettings["EtranConfig"];
        }
    }

    public static string DBConnName
    {
        get
        {
            return ConfigurationManager.AppSettings["DBConnNameService"];
        }
    }

    public static string DBConn
    {
        get
        {
            return GlobalObjectsManager.GetDBConn();
        }
    }
}
