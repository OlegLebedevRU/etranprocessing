using System;
using System.Configuration;



/// <summary>
/// Менеджер конфигурации службы.
/// </summary>
public sealed class EtranConfigurationManager
{

    static public string Cred
    {
        get
        {
            return ConfigurationManager.AppSettings["Cred"];
        }
    }

    static public string CertName
    {
        get
        {
            return ConfigurationManager.AppSettings["CertName"];
        }
    }
    static public string Url
    {
        get
        {
            return ConfigurationManager.AppSettings["Url"];
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
