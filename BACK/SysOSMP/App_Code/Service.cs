using System;
using System.Collections.Specialized;
using System.Web;
using System.Web.Services;
using System.Web.Services.Protocols;
using System.Web.UI.WebControls;
using System.Xml;


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
    public string XmlToXml(string xmlFilePath)
    {
        string ret = "OK";
        try
        {
            XmlDocument doc = new XmlDocument();
            doc.Load(xmlFilePath);
            return bl.XmlToXml(doc.OuterXml);
        }
        catch (Exception ex)
        {
            ret = ex.Message;
        }
        return ret;
    }

    [WebMethod]
    public string DoRequest(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek)
    {
        return new bl(Function, PaymExtId, PaymSubjTp, Amount, Params, ConnectionTimeout, Rek).DoRequest();
    }


    [WebMethod]
    //public string PlaterraTspMigrate(string from, string to)
    public string PlaterraTspMigrate()
    {
        string from = "PlaterraProviders.xml";
        string to = "Providers.xml";
        return GlobalObjectsManager.PlaterraTspMigrate(from, to);
    }


    [WebMethod]
    public string getProviders()
    {
        //return new bl("getProviders", "1", "1", "1", "1 1", 30, "8703242;36Vaso02;Mr36Vaso02").SendAndSave("providers", "getProviders", "Providers");
        //return new bl("getProviders", "1", "1", "1", "1 1", 30, "9021787;44591316;kpeCsCgGBZ").SendAndSave("providers", "getProviders", "Providers");
        NameValueCollection attr = new NameValueCollection();
        //attr.Add("mode", "async");
        attr.Add("quid", "101117939");
        return new bl("getProviders", "1", "1", "1", "1 1", 30, "9808694;vadimsotex6;ZpEA5tkJRT").SendAndSave("providers", "getProviders", attr, "Providers");
    }

    //3528898810  10170994324001 
    //1145616634  10170559298001 

    //443195270 	10170994498001
    //2354880390  10170559232001

    //3528243452  10170993919001 
    //1144961276  10170559199001 

    //2000860754  10170995566001 
    //3147871826  10170558904001 

    //1151405963  10170994713001 
    //3534688139  10170560629001 

    //2747394633  10170994671001 
    //364112457   10170559056001 


    [WebMethod]
    public string CancelPayment(string id)//, string rek)
    {
        string rek = "rek=8703242:36Vaso02:Mr36Vaso02";
        return new bl("cancel", "1", "1", "1", "1 1", 30, rek).CancelPayment("3528898810");
    }

    [WebMethod]
    public string GetReestr(DateTime from, DateTime to, string rek)
    //public string GetReestr()
    {
        //DateTime from;
        //DateTime to;
        //string rek;
        //from = DateTime.Parse("2011-07-18");
        //to = DateTime.Parse("2011-07-19");
        //rek = "rek=9067568:Makarov74:YAKxHHbGHU";
        return Reestr.GetReestr(from, to, rek);
    }

    [WebMethod]
    public string getResultCodes()
    {
        return new bl("getResultCodes", "1", "1", "1", "1 1", 30, "8703242;36Vaso02;Mr36Vaso02").SendAndSave("system", "getResultCodes", null, "ResultCodes");
    }

    [WebMethod]
    public long GetAmount(string Rek)
    {
        long amount = 0;
        GlobalObjectsManager.Logger.Info("GetAmount START...");
        //Rek = "8703242;36Vaso02;Mr36Vaso02";
        try
        {
            amount = new bl("balance", "1", "1", "1", "1 1", 30, Rek).GetAmount();
        }
        catch (Exception e)
        {
            GlobalObjectsManager.Logger.Error(e);
        }
        GlobalObjectsManager.Logger.Info("GetAmount RET: " + amount);
        return amount;
    }



    [WebMethod]
    public string Test()
    {
        string result = "EMPTY";
        try
        {
            //9003_101028_133612VT PaymSubjTp:101 Amount: 1280 Params: 188 9645243359;1001  ConnectionTimeout: 90 Rek:rek=8703242:36Vaso02:Mr36Vaso02;terminal=9003;pdt=2010-10-28 13:40:38

            //result = new bl("payment", "20110725-0001", "609", "100", "1 231262862960;2 9046142957", 60, "rek=9808694:vadimsotex6:ZpEA5tkJRT;org_id=302;terminal=433;pdt=2010-10-28 13:40:38").DoRequest();

            result = new bl("payment", "20110725-0001", "100", "100", "14 9162376067", 60, "rek=8703242:36Vaso02:Mr36Vaso02;terminal=9003;pdt=2010-10-28 13:40:38").DoRequest();

        }
        catch (Exception e)
        {
            GlobalObjectsManager.Logger.Error(e);
            result = e.Message;
        }
        return result;
    }

}
