using System;
using System.Configuration;




/// <summary>
/// Менеджер конфигурации службы.
/// </summary>
public sealed class EtranConfigurationManager
{

    static public string DBServiceConn
    {
        get
        {
            return ConfigurationManager.ConnectionStrings["Service"].ConnectionString;
        }
    }

    static public string MessageProcessor
    {
        get
        {
            return ConfigurationManager.AppSettings["MessageProcessor"];
        }
    }

    static public string FailedPayLog
    {
        get
        {
            return GlobalObjectsManager.curr_path + ConfigurationManager.AppSettings["FailedPayLog"] + @"/";
        }
    }

    static public string FixPayLog
    {
        get
        {
            return GlobalObjectsManager.curr_path + ConfigurationManager.AppSettings["FixPayLog"] + @"/";
        }
    }

    static public string PayLog
    {
        get
        {
            return ConfigurationManager.AppSettings["PayLog"];
        }
    }

    static public int Interval
    {
        get
        {
            return int.Parse(ConfigurationManager.AppSettings["Interval"]) * 60 * 1000;
        }
    }

}
