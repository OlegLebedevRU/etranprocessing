using System.Web.Services;

[WebService(Namespace = "http://tempuri.org/")]
[WebServiceBinding(ConformsTo = WsiProfiles.BasicProfile1_1)]
public class Service : WebService
{
    [WebMethod]
    public string DoRequest(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek, string TotalSum)
    {
        return new bl(Function, PaymExtId, PaymSubjTp, Amount, Params, ConnectionTimeout, Rek, TotalSum).DoRequest();
    }

    [WebMethod]
    public long GetAmount(string Rek)
    {
        //Rek = "rek=test2";
        return new bl("GetAmount", "GetAmount", "1", "1", "1 1", 60, Rek, "0").GetAmount();
    }

    [WebMethod]
    public string UpdateOperatorsDb(string Rek)
    {
        //Rek = "rek=test2";
        return new bl("GetAmount", "GetAmount", "1", "1", "1 1", 60, Rek, "0").UpdateOperatorsDb();
    }


    [WebMethod]
    public string Test()
    {
        return new bl("check", "20131125-0001", "101", "500", "290 9219770770;1001 9219770770", 90, "rek=test2;terminal=732;pdt=2014-06-19 15:52:46", "30").DoRequest();
        //return new bl("payment", "20131125-0001", "431", "500", "1 123456789;2 ||11||12|2013;3 ||11:1234||12:6543", 90, "rek=test2", "30").DoRequest();
    }

}
