using System;
using System.Configuration;



/// <summary>
/// Менеджер конфигурации службы.
/// </summary>
public sealed class EtranConfigurationManager
{

    static public string TSP
    {
        get
        {
            return ConfigurationManager.AppSettings["TSP"];
        }
    }

    static public string CyberParams
    {
        get
        {
            return ConfigurationManager.AppSettings["CyberParams"];
        }
    }

    static public string CyberGate
    {
        get
        {
            return ConfigurationManager.AppSettings["CyberGate"];
        }
    }

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
