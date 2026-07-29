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
            return ConfigurationManager.AppSettings["DBConnNamePayments"];
        }
    }

    static public string DBConn
    {
        get
        {
            return GlobalObjectsManager.DBConn;
        }
    }

    static public string UpdateOperatorsDB
    {
        get
        {
            return ConfigurationManager.AppSettings["UpdateOperatorsDB"];
        }
    }

    static public string Url
    {
        get
        {
            return ConfigurationManager.AppSettings["Url"];
        }
    }
}
