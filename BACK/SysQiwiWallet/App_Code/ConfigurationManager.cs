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

    static public string DBConnNamePayments
    {
        get
        {
            return ConfigurationManager.AppSettings["DBConnNamePayments"];
        }
    }

    static public string PaymentDbConnectionString
    {
        get
        {
            return GlobalObjectsManager.PaymentDbConnectionString;
        }
    }

    static public string GetServiceID(string tsp_code)
    {
        return ConfigurationManager.AppSettings["tsp_" + tsp_code];
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

