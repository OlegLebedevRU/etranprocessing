using System;
using System.Web;
using System.Web.Services;
using System.Web.Services.Protocols;

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
    public string DoRequest(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek, string TotalSum)
    {
        return new bl(Function, PaymExtId, PaymSubjTp, Amount, Params, ConnectionTimeout, Rek, TotalSum).DoRequest();
    }

    [WebMethod]
    public long GetAmount(string Rek)
    {
        return new bl("GetAmount", "GetAmount", "1", "1", "1 1", 60, Rek, "0").GetAmount();
    }


    [WebMethod]
    public string Test()
    {
        //return new bl("payment", "20101118-0002", "100", "350", "14 9162376067", 90, "rek=kassa:Bambarbiya3321", "30").DoRequest();
        return new bl("payment", "20101118-0002", "100", "150", "14 8888888888", 90, "rek=kassa:Bambarbiya3321", "30").DoRequest();
    }

}
