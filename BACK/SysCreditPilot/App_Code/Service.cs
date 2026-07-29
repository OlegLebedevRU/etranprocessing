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
    public string UpdateOperatorsDb(string Rek)
    {
        //return new bl("UpdateOperatorsDb", "UpdateOperatorsDb", "1", "1", "1 1", 60, "rek=sol_oper:testtest", "0").UpdateOperatorsDb();
        //return new bl("UpdateOperatorsDb", "UpdateOperatorsDb", "1", "1", "1 1", 60, "rek=platerraop:UD5vso)Udlvv]Ux", "0").UpdateOperatorsDb();
        return new bl("UpdateOperatorsDb", "UpdateOperatorsDb", "1", "1", "1 1", 60, Rek, "0").UpdateOperatorsDb();
    }


    [WebMethod]
    public string Test()
    {
        //return new bl("payment", "20101118-0002", "100", "9500", "1 123456", 90, "rek=sol_oper:testtest:60:60:20110608", "30").DoRequest();
        //return new bl("payment", "20101118-0003", "100", "9500", "1 123456", 90, "rek=sol_oper:testtest:60:60:20110608", "30").DoRequest();
        //return new bl("payment", "20101118-0004", "100", "9500", "1 123456", 90, "rek=sol_oper:testtest:60:60:20110608", "30").DoRequest();
        return new bl("check", "20101118-0005", "100", "9500", "14 9162376067", 90, "rek=sol_oper:testtest:60:60:20110608", "30").DoRequest();
        //return new bl("check", "20130116-0001", "715", "1500", "1 111111111111111111111;2 123456;3 договор;4 фам;5 имя;6 отч;7 9162376067", 90, "rek=platerraop:UD5vso)Udlvv]Ux", "30").DoRequest();
    }

}
