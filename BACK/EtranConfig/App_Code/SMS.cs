using System;
using System.Data;
using System.Configuration;
using System.IO;
using System.Net;
using System.Text;
using System.Text.RegularExpressions;
using System.Web;
using System.Web.Configuration;
using System.Web.Security;
using System.Web.UI;
using System.Web.UI.HtmlControls;
using System.Web.UI.WebControls;
using System.Web.UI.WebControls.WebParts;
using System.Xml;
using EtranLib.Net;

/// <summary>
/// Сводное описание для SMS
/// </summary>
public class SMS
{
	public SMS()
	{
	}

    static string CheckAccount(string _number)
    {
        string number = _number;
        try
        {
            if (number.Length == 11)
            {
                if (number[0] != '7')
                    number = "7" + number.Remove(0, 1);
                return number;
            }
            else
                if (number.Length == 10)
                {
                    number = "7" + number;
                    return number;
                }
        }
        catch
        {
        }
        return null;

    }


    static private string MobileMoney(long phone, string msg, bool balance)
    {
        string result = string.Empty;
        string id = EtranLib.RandomAndPassword.RandomPassword.Generate(9);

        //string data = "login=" + EtranConfigurationManager.SMS_login + "&password=" + EtranConfigurationManager.SMS_pwd + "&phones=" + phone + "&message=" + msg + "&originator=Terminal";
        string data = "";
        if (balance)
            data = "<?xml version=\"1.0\" encoding=\"windows-1251\"?><request method=\"CheckBalance\"><login>" + EtranConfigurationManager.SMS_login + "</login><pwd>"+ EtranConfigurationManager.SMS_pwd + "</pwd></request>";
        else
            data = "<request method=\"SendSMS\"><login>" + EtranConfigurationManager.SMS_login + "</login><pwd>"
            + EtranConfigurationManager.SMS_pwd + "</pwd><originator>Terminal</originator><phone_to>+" + phone + "</phone_to><message>"
            + msg + "</message><sync>" + id.ToString() + "</sync></request>";

        GlobalObjectsManager.Logger.Info("SMS Sent TRY: " + data);
        string ret = Net.XmlPost(EtranConfigurationManager.SMS_url, EtranConfigurationManager.SMSTimeOut, data);
        GlobalObjectsManager.Logger.Info("SMS Sent RECV: " + ret);
        XmlDocument xml_doc = new XmlDocument();
        xml_doc.LoadXml(ret);
        if (balance)
        {
            string account = xml_doc.SelectSingleNode("/response/balance").InnerText;
            account = account.Replace('.', ',');
            return account + " руб.";
        }
        result = xml_doc.SelectSingleNode("/response/sms").Attributes["id"].Value;
        return result;
    }


    //protected void sms_balance()
    //{
    //    try
    //    {
    //        string result = string.Empty;
    //        string post = "<?xml version=\"1.0\" encoding=\"windows-1251\"?><request method=\"CheckBalance\"><login>payter</login><pwd>095746620</pwd></request>";
    //        string url = "http://send.smsmm.ru/";
    //        Net.Init();
    //        string ret = Net.XmlPost(url, 10000, post);
    //        string templ = "<?xml version";
    //        int indx = ret.IndexOf(templ);
    //        ret = ret.Substring(indx);
    //        GlobalObjectsManager.Logger.Info("sms_balance RET: " + ret);
    //        XmlDocument xml_doc = new XmlDocument();
    //        xml_doc.LoadXml(ret);
    //        string account = xml_doc.SelectSingleNode("/response/balance").InnerText;
    //        account = account.Replace('.', ',');
    //        tb_sms_balance.Text = account ;
    //    }
    //    catch (Exception e) 
    //    {
    //        tb_sms_balance.Text = "задержка в обновлении.";//e.Message; 
    //        GlobalObjectsManager.Logger.Error("sms_balance", e);
    //    }
    //}
    private static string SmsBalanceSMSTraffic(out int smsLeft)
    {

        string data = string.Format("login={0}&password={1}&operation=account", WebConfigurationManager.AppSettings["SMS_LOGIN_SMSTRAFFIC"], WebConfigurationManager.AppSettings["SMS_PASSWORD_SMSTRAFFIC"]);
        HttpWebRequest httpGET = (HttpWebRequest)WebRequest.Create(new Uri(WebConfigurationManager.AppSettings["SMS_URL_SMSTRAFFIC"] + "?" + data));

        String resp = string.Empty;
        try
        {
            HttpWebResponse response = (HttpWebResponse)httpGET.GetResponse();
            Encoding encoding = Encoding.GetEncoding("windows-1251");
            StreamReader sReader = new StreamReader(response.GetResponseStream(), encoding);
            resp = sReader.ReadToEnd();
            response.Close();
            // parser = new HtmlParser(resp);
        }
        catch (Exception exc)
        {
            smsLeft = 0;
            return exc.Message;
        }
        XmlDocument xdoc = new XmlDocument();
        xdoc.LoadXml(resp);
        string account = xdoc.SelectSingleNode("/reply/account").InnerText;
        account = Regex.Replace(account, "sms", string.Empty).Trim();
        
        int.TryParse(account, out smsLeft);
        
        return "OK";
    }
    private static string SendSMSTraffic(string phone, string msg, string fromWhom, out int number)
    {
        string result = string.Empty;
        //string data = "login=" + WebConfigurationManager.AppSettings["SMS_LOGIN_SMSTRAFFIC"] + "&password=" + WebConfigurationManager.AppSettings["SMS_PASSWORD_SMSTRAFFIC"] + "&phones=" + phone + "&message=" + msg + "&originator=" + fromWhom + "&rus=5&max_parts=10000";
        string data = "login=" + WebConfigurationManager.AppSettings["SMS_LOGIN_SMSTRAFFIC"] + "&password=" + WebConfigurationManager.AppSettings["SMS_PASSWORD_SMSTRAFFIC"] + "&phones=" + phone + "&message=" + msg + (string.IsNullOrEmpty(fromWhom) ? "" : "&originator=" + fromWhom) + "&rus=5";
        GlobalObjectsManager.Logger.Info(data);

        
        //HttpWebRequest httpGET = (HttpWebRequest)WebRequest.Create(new Uri(WebConfigurationManager.AppSettings["SMS_URL_SMSTRAFFIC"] + "?" + data));
        try
        {
            result = Net.WebRequest(WebConfigurationManager.AppSettings["SMS_URL_SMSTRAFFIC"], 15000, data, null);
            //HttpWebResponse response = (HttpWebResponse)httpGET.GetResponse();
            //Encoding encoding = Encoding.UTF8;
            //StreamReader sReader = new StreamReader(response.GetResponseStream(), encoding);
            //result = sReader.ReadToEnd();
            //GlobalObjectsManager.Logger.Info(result);
            //response.Close();
        }
        catch (Exception exc)
        {
            number = 0;
            return exc.Message;
        }
        XmlDocument xml_doc = new XmlDocument();
        xml_doc.LoadXml(result);
        XmlNode node = xml_doc.SelectSingleNode("/reply/result");
        result = node != null ? node.InnerText : string.Empty;
        node = xml_doc.SelectSingleNode("/reply/description");
        string numberStr = result.ToUpper() == "OK" ? (node != null ? node.InnerText.Replace("queued", string.Empty).Replace("messages", string.Empty).Trim() : "0") : "0";
        int.TryParse(numberStr, out number);
        return result.ToUpper() == "OK" ? result.ToUpper(): result;
    }
    static private string smstraffic(long phone, string msg, bool balance)
    {
        GlobalObjectsManager.Logger.Info("SMS TRY: " + phone + " msg: " + msg);
        string result = string.Empty;
        //string data = "login=" + EtranConfigurationManager.SMS_login + "&password=" + EtranConfigurationManager.SMS_pwd;
        string data = "login=" + EtranConfigurationManager.SMS_login + "&psw=" + EtranConfigurationManager.SMS_pwd;
        if (balance)
            data += "";
        else
            data += "&phones=" + phone + "&mes=" + msg;

        //if(balance)
        //    data += "&operation=account";
        //else
        //    data += "&phones=" + phone + "&message=" + msg + "&originator=Terminal";

        string url = balance ? EtranConfigurationManager.SMS_url_balance : EtranConfigurationManager.SMS_url;
        string ret = Net.WebRequest(url, EtranConfigurationManager.SMSTimeOut, data, null);
        GlobalObjectsManager.Logger.Info("SMS Provider ret: " + ret);
        if (balance)
        {
            result = ret;
        }
        else
            if (ret.IndexOf("OK") > -1 && ret.IndexOf("ID") > -1)
            result = "OK";
        else
        {
            result = "ERROR";
        }

        //XmlDocument xml_doc = new XmlDocument();
        //xml_doc.LoadXml(ret);
        //if(balance)
        //    result = xml_doc.SelectSingleNode("/reply/account").InnerText;
        //else
        //    result = xml_doc.SelectSingleNode("/reply/result").InnerText;

        GlobalObjectsManager.Logger.Info("SMS Sent: " + result);
        return result;
    }
    
    static int count = 0;
    static public string Send(string _phone, string msg, bool balance, string provider, string fromWhom)
    {
	GlobalObjectsManager.Logger.Info("SMS Send _phone: " + _phone + " provider " + provider);
        int smsCount = 0;
        XmlDocument xdoc = new XmlDocument();
        
        string message = string.Empty;
        if(provider=="smstraffic")
        {
            if(balance)
            {
                message = SmsBalanceSMSTraffic(out smsCount);
            }
            else
            {
                _phone = CheckAccount(_phone);
                message = SendSMSTraffic(_phone, msg, fromWhom, out smsCount);
            }
            
        }
        else
        {
            smsCount = 0;
            message = "неверно указан провайдер";
        }
        xdoc.LoadXml("<?xml version=\"1.0\" encoding=\"windows-1251\"?><response></response>");
        XmlNode root = xdoc.DocumentElement;
        XmlElement elem = xdoc.CreateElement("smscount");
        elem.InnerText = smsCount.ToString();
        
        root.AppendChild(elem);
        XmlElement elem2 = xdoc.CreateElement("message");
        elem2.InnerText = message;
        root.AppendChild(elem2);

        return xdoc.OuterXml;
    }

    static public string Send(string _phone, string msg, bool balance)
    {
        string ret = "ERROR";
        try
        {
            if (balance)
            {
                return smstraffic(0, "", true);
            }
            else
            {

                string num = CheckAccount(_phone);
                long phone = long.Parse(num);
                string r = smstraffic(phone, msg, false);
                if (r.ToUpper() == "OK")
                    return "OK";
            }

            //if (balance)
            //{
            //    return MobileMoney(0, "", true);
            //}
            //else
            //{
            //    string num = CheckAccount(_phone);
            //    long phone = long.Parse(num);
            //    string r = MobileMoney(phone, msg, false);
            //    //if (r.ToUpper() == "OK")
            //        return "OK";
            //}


        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error("Send", ex);
        }

        return ret;
    }

}
