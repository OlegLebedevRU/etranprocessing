using System;
using System.Web;
using System.Web.Services;
using System.Web.Services.Protocols;
using System.Xml;

[WebService(Namespace = "http://tempuri.org/")]
[WebServiceBinding(ConformsTo = WsiProfiles.BasicProfile1_1)]
public class Service : System.Web.Services.WebService
{
    public Service () {

        //Раскомментируйте следующую строку в случае использования сконструированных компонентов 
        //InitializeComponent(); 
    }



    [WebMethod]
    public string DoRequest(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek)
    {
        return new bl(Function, PaymExtId, PaymSubjTp, Amount, Params, ConnectionTimeout, Rek).DoRequest();
    }



    [WebMethod]
    //public string Test(string ID, string amount)
    public string Test()
    {
        string result = "EMPTY";
        try
        {
            //result = new bl("payment", ID, "2000", amount, "1 60003455", 60, "1").DoRequest();
            //result = new bl("check", "PAN20100128-0002", "2003", "150", "1 50760003455", 60, "1").DoRequest();
            result = new bl("check", "PAN20100217-0001", "2003", "0", "1  60003455", 60, "1").DoRequest();
             
        }
        catch (Exception e)
        {
            GlobalObjectsManager.Logger.Error(e);
            result = e.Message;
        }
        return result;
    }
    
}
