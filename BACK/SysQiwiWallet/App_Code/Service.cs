using System;
using System.Web;
using System.Web.Services;
using System.Web.Services.Protocols;
using System.Xml;
using System.Collections.Specialized;



[WebService(Namespace = "http://tempuri.org/")]
[WebServiceBinding(ConformsTo = WsiProfiles.BasicProfile1_1)]
public class Service : System.Web.Services.WebService
{
    public Service()
    {

        //Раскомментируйте следующую строку в случае использования сконструированных компонентов 
        //InitializeComponent(); 
    }

    [WebMethod]
    public string DoRequest(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek)
    {
        return new bl(Function, PaymExtId, PaymSubjTp, Amount, Params, ConnectionTimeout, Rek).DoRequest();
    }

    [WebMethod]
    public long GetAmount(string Rek)
    {
        long amount = 0;
        GlobalObjectsManager.Logger.Info("GetAmount START...");

        try
        {

            NameValueCollection m_Rek = EtranLib.Collection.Collection.GetNameValueCollection(Rek, ";", "=");

            string rrr = m_Rek["rek"];
            GlobalObjectsManager.Logger.Info("GetAmount REK: " + rrr);
            string[] Splitter = (m_Rek["rek"] != null) ? m_Rek["rek"].Split(':') : Rek.Split(';');
            GlobalObjectsManager.Logger.Info("GetAmount REK Splitter LEN: " + Splitter.Length);
            for (int i = 0; i < Splitter.Length; i += 2)
            {
                string TerminalID = Splitter[i];
                string PWD = Splitter[i + 1];
                GlobalObjectsManager.Logger.Info("GetAmount ACCOUNTS N: " + (i / 2) + " TerminalID= " + TerminalID + " PWD= " + PWD);

                string result = new bl("balance", "1", "1", "1", "1 1", 30, "rek=" + TerminalID + ":" + PWD ).DoRequest();
                XmlDocument doc = new XmlDocument();
                doc.LoadXml(result);
                string balance = doc.SelectSingleNode("Response/Description").InnerText;
                GlobalObjectsManager.Logger.Error("GetAmount for TerminalID=" + TerminalID + " balance: " + balance);
                try
                {
                    amount += long.Parse(balance);
                }
                catch (Exception ex)
                {
                    GlobalObjectsManager.Logger.Error("GetAmount TRYPARSE balance", ex);
                }
            }
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error("GetAmount", ex);
        }

        GlobalObjectsManager.Logger.Info("GetAmount RET: " + amount);
        return amount;
    }

    [WebMethod]
    public string Reestr(string from, string to, string rek)
    {
        return new bl("reestr", "1", "1", "1", "1 " + from + ";2 " + to, 30, rek).DoRequest();
    }

    [WebMethod]
    public string ResultCodes()
    {
        return new bl("resultcodes", "1", "1", "1", "1 111111", 30, "rek=9162376067:gucg58d;terminal=9003").DoRequest();
    }

    [WebMethod]
    public string Providers()
    {

        return new bl("Providers", "1", "1", "1", "1 111111", 30, "rek=9162376067:22b75c7726ebe71ba6aa22e9681ad5").DoRequest();
        //return new bl("Providers", "1", "1", "1", "1 111111", 30, "rek=9162376067:iz1osxy4;terminal=9003").DoRequest();
    }

    [WebMethod]
    public string GetToken(string phone, string pwd, string hexcode, string vcode)
    {
        //return bl.GetToken("9162376067", "iz1osxy4", "9046f39a376b403f253a91c35a6d3", "862761");
        return bl.GetToken(phone, pwd, hexcode, vcode);
    }


    [WebMethod]
    public string Test()
    {
        string result = "EMPTY";
        try
        {

            //result = new bl("payment", "20110704-0013", "100", "50", "14 9162376067", 30, "rek=9162376067:gucg58d;terminal=9003;pdt=2010-10-28 13:40:38").DoRequest();
            //result = new bl("payment", "20110705-0001", "100", "50", "14 9162376067", 30, "rek=9162376067:gucg58d;terminal=9003;pdt=2010-10-28 13:40:38").DoRequest();
            //result = new bl("payment", "20110705-0002", "100", "150", "14 9162376067", 30, "rek=9161234455:redf56:9162376067:gucg58d;terminal=9003;pdt=2010-10-28 13:40:38").DoRequest();
            result = new bl("payment", "20111111-0001", "100", "9500", "14 9162376067", 30, "rek=9162376067:22b75c7726ebe71ba6aa22e9681ad5;terminal=9003;pdt=2010-10-28 13:40:38").DoRequest();

        }
        catch (Exception e)
        {
            GlobalObjectsManager.Logger.Error(e);
            result = e.Message;
        }
        return result;
    }

}
