using System;
using System.Web;
using System.Web.Services;
using System.Web.Services.Protocols;

[WebService(Namespace = "http://tempuri.org/")]
[WebServiceBinding(ConformsTo = WsiProfiles.BasicProfile1_1)]
public class Service : System.Web.Services.WebService
{
    public Service () {

        //Раскомментируйте следующую строку в случае использования сконструированных компонентов 
        //InitializeComponent(); 
    }


    [WebMethod]
    public string DoRequest(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek, string TotalSum)
    {
        return new bl(Function, PaymExtId, PaymSubjTp, Amount, Params, ConnectionTimeout, Rek, TotalSum).DoRequest();
    }

    [WebMethod]
    public string UpdateDBErrors()
    {
        return bl.UpdateDBErrors();
    }

    [WebMethod]
    public long GetAmount(string Rek)
    {
        return new bl("GetAmount", "GetAmount", "1", "1", "1 1", 60, Rek,"0").GetAmount();
    }

    [WebMethod]
    public string UpdateOperatorsDB()
    {
        return bl.UpdateOperatorsDB();
    }


    [WebMethod]
    public string Test()
    {
        string result = "EMPTY";
        try
        {
            //result = new bl("payment", "0660_110310_07512321", "126", "9700", "1 9262291179", 60, "0197;terminal=660;pdt=2010-11-03 15:56:19").DoRequest();
            //result = new bl("payment", "0660_220411_07512321", "126", "9700", "1 9262291179", 60, "0197;terminal=660;pdt=2010-11-03 15:56:19").DoRequest();
            //result = new bl("payment", "0660_220411_17521111", "126", "9700", "1 9262291179", 60, "0197;terminal=660;pdt=2010-11-03 15:56:19;rek=DELTAPAY").DoRequest();
            //result = new bl("payment", "0660_220411_17521122", "126", "97000", "1 9262291179", 60, "0197;terminal=660;pdt=2010-11-03 15:56:19;rek=DELTAPAY").DoRequest();

            result = new bl("fhdshg", "0660_110410_07512321", "609", "5500", "1 12345678911;2 9153257766", 60, "0197;terminal=660;pdt=2010-11-03 15:56:19;rek=SIL28004445.00022517:4445","0").DoRequest();
        }
        catch (Exception e)
        {
            GlobalObjectsManager.Logger.Error(e);
            result = e.Message;
        }
        return result;
    }
    
}
