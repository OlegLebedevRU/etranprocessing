using System;
using System.Configuration;
using System.Web.Services;




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
    {
        try
        {


           // string param_in = "Function: " + Function + " PaymExtId: " + PaymExtId + " PaymSubjTp:" + PaymSubjTp + " Amount: " + Amount + " Params: " + Params + " ConnectionTimeout: " + ConnectionTimeout + " Rek:" + Rek + " TotalSum " + TotalSum;

            return "<Response><Result>OK</Result><OperatorAccepted>" + DateTime.Now + "</OperatorAccepted><PaymExtId>" + PaymExtId + "</PaymExtId><Description>OK</Description></Response>";

            //        return "<Response><Result>OK</Result><os_id>8</os_id><Status>0</Status><OperatorAccepted>" + DateTime.Now.ToString() + "</OperatorAccepted><PaymExtId>" + PaymExtId + "</PaymExtId><Description>успешно обработан.</Description></Response>";
            //	return "<Response><Result>ERROR</Result><os_id>8</os_id><Status>-1</Status><PaymExtId>" + PaymExtId + "</PaymExtId><Description>ERROR.</Description></Response>";

            //return "<Response><Result>ERROR</Result><PaymExtId>" + PaymExtId + "</PaymExtId><Description>TEST MODE ERROR</Description></Response>";


        }
        catch (Exception e)
        {
        }
        return "<Response><Status>-1</Status><OperatorAccepted></OperatorAccepted><Result>Error</Result><PaymExtId></PaymExtId><Description>Системная ошибка.</Description></Response>";
    }
}
