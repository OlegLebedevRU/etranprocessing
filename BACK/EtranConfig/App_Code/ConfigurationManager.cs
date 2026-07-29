using System;
using System.Configuration;



/// <summary>
/// Менеджер конфигурации службы.
/// </summary>
public sealed class EtranConfigurationManager
{


    static public string GetXmlResponse(string function)
    {
        string check = @"<Response><Result>%code%</Result><PaymExtId>%id%</PaymExtId><Description>%desc%</Description></Response>";
        return check;
        //return (function.ToLower() == "payment") ? check.Insert("<Response>".Length, "<Status>%status%</Status><OperatorAccepted>%oa%</OperatorAccepted>") : check;
    }

    static public string DBConn
    {
        get
        {
            return ConfigurationManager.ConnectionStrings["DBConn"].ConnectionString;
        }
    }
    static public int SMSTimeOut
    {
        get
        {
            return int.Parse(ConfigurationManager.AppSettings["SMSTimeOut"]) * 1000;
        }
    }

    static public string SMS_login
    {
        get
        {
            return ConfigurationManager.AppSettings["SMS_login"];
        }
    }

    static public string SMS_url
    {
        get
        {
            return ConfigurationManager.AppSettings["SMS_url"];
        }
    }
    static public string SMS_url_balance
    {
        get
        {
            return ConfigurationManager.AppSettings["SMS_url_balance"];
        }
    }

    static public string SMS_pwd
    {
        get
        {
            return ConfigurationManager.AppSettings["SMS_pwd"];
        }
    }


}