using System;
using System.Configuration;



/// <summary>
/// Менеджер конфигурации службы.
/// </summary>
public sealed class EtranConfigurationManager
{


    static public int Region
    {
        get
        {
            return int.Parse(ConfigurationManager.AppSettings["Region"]);
        }
    }

    static public int BankCode
    {
        get
        {
            return int.Parse(ConfigurationManager.AppSettings["BankCode"]);
        }
    }

    static public string OperCodes
    {
        get
        {
            return ConfigurationManager.AppSettings["OperCodes"];
        }
    }
static public string OrgOperCodes
    {
        get
        {
            return ConfigurationManager.AppSettings["OrgOperCodes"];
        }
    }

    static public string DBsrc
    {
        get
        {
            return ConfigurationManager.AppSettings["DBsrc"];
        }
    }

    static public string SOGBackup
    {
        get
        {
            return GlobalObjectsManager.curr_path + "SOGBackup\\";
        }
    }

    static public string DBdst
    {
        get
        {
            return GlobalObjectsManager.curr_path + "BAZA\\";
        }
    }

    static public string ReestrSrc
    {
        get
        {
            return GlobalObjectsManager.curr_path + "SOG";
        }
    }

    static public string ReestrDst
    {
        get
        {
            return ConfigurationManager.AppSettings["ReestrDst"];
            //return @"C:\123\";
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

    static public string DBConn_Platerra
    {
        get
        {
            return GlobalObjectsManager.DbConnectionString;
        }
    }

    static public string DBConn_Zhivago
    {
        get
        {
            return ConfigurationManager.ConnectionStrings["Zhivago"].ConnectionString;
        }
    }
    static public string DbSpName
    {
        get
        {
            return ConfigurationManager.AppSettings["DbSpName"];
        }
    }

}
