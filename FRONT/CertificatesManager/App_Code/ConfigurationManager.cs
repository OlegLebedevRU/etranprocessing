using System;
using System.Configuration;



/// <summary>
/// Менеджер конфигурации службы.
/// </summary>
public sealed class EtranConfigurationManager
{

    static public bool IsProxyCA
    {
        get
        {
            return bool.Parse(ConfigurationManager.AppSettings["IsProxyCA"].ToString());
        }
    }

    static public string CertificateTemplate
    {
        get
        {
            return ConfigurationManager.AppSettings["CertificateTemplate"].ToString();
        }
    }

    static public string RootCAUrl
    {
        get
        {
            return ConfigurationManager.AppSettings["RootCAUrl"].ToString();
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

    static public string SignKey
    {
        get
        {
            return ConfigurationManager.AppSettings["SignKey"].ToString();
        }
    }

}
