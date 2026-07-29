using System;
using System.Collections.Specialized;
using System.Web;
using System.Web.Services;
using System.Web.Services.Protocols;

[WebService(Namespace = "http://tempuri.org/")]
[WebServiceBinding(ConformsTo = WsiProfiles.BasicProfile1_1)]
public class Service : System.Web.Services.WebService
{
    public Service()
    {

        //Uncomment the following line if using designed components 
        //InitializeComponent(); 
    }

    [WebMethod]
    public string DoRequest(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek, string TotalSum)
    // public string DoRequest(string Function)
    {
        try
        {


            //      System.Threading.Thread.Sleep(int.Parse(Amount));

            //GlobalObjectsManager.Logger.Info(Function);

            string param_in = "Function: " + Function + " PaymExtId: " + PaymExtId + " PaymSubjTp:" + PaymSubjTp + " Amount: " + Amount + " Params: " + Params + " ConnectionTimeout: " + ConnectionTimeout + " Rek:" + Rek + " TotalSum " + TotalSum;
            GlobalObjectsManager.Logger.Info("param_in: " + param_in);

            NameValueCollection mPaymParams = Collection.GetNameValueCollection(Params);
            string respDescr =  mPaymParams["2"];

            return "<Response><Result>OK</Result><OperatorAccepted>" + DateTime.Now + "</OperatorAccepted><PaymExtId>" + PaymExtId + "</PaymExtId><Description>" + respDescr + "</Description></Response>";

            //        return "<Response><Result>OK</Result><os_id>8</os_id><Status>0</Status><OperatorAccepted>" + DateTime.Now.ToString() + "</OperatorAccepted><PaymExtId>" + PaymExtId + "</PaymExtId><Description>успешно обработан.</Description></Response>";
            //	return "<Response><Result>ERROR</Result><os_id>8</os_id><Status>-1</Status><PaymExtId>" + PaymExtId + "</PaymExtId><Description>ERROR.</Description></Response>";

            //return "<Response><Result>ERROR</Result><PaymExtId>" + PaymExtId + "</PaymExtId><Description>TEST MODE ERROR</Description></Response>";


        }
        catch (Exception e)
        {
            GlobalObjectsManager.Logger.Error(e);
        }
        return "<Response><Status>-1</Status><OperatorAccepted></OperatorAccepted><Result>Error</Result><PaymExtId></PaymExtId><Description>Системная ошибка.</Description></Response>";
    }

}
