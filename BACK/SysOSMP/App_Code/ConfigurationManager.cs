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

    static public string DbConnectionString
    {
        get
        {
            return GlobalObjectsManager.DbConnectionString;
        }
    }

    static public int Interval
    {
        get
        {
            return int.Parse(ConfigurationManager.AppSettings["Interval"]);
        }
    }

    static public int StatusWait
    {
        get
        {
            return int.Parse(ConfigurationManager.AppSettings["StatusWait"]);
        }
    }


    static public string PaymentUrl
    {
        get
        {
            return ConfigurationManager.AppSettings["PaymentUrl"];
        }
    }
}
