using System;
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

    static public string DBConnectionString
    {
        get
        {
            return GlobalObjectsManager.DbConnectionString;
        }
    }
    static public string version
    {
        get
        {
            return ConfigurationManager.AppSettings["version"];
        }
    }
    static public string agentId
    {
        get
        {
            return ConfigurationManager.AppSettings["agentId"];
        }
    }
    static public string salepointId
    {
        get
        {
            return ConfigurationManager.AppSettings["salepointId"];
        }
    }
    static public string deviceId
    {
        get
        {
            return ConfigurationManager.AppSettings["deviceId"];
        }
    }
    static public int regionId
    {
        get
        {
            return int.Parse(ConfigurationManager.AppSettings["regionId"]);
        }
    }

}
