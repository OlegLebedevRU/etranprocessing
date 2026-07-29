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
    public string Test()
    {
        return new bl("check", "1183_140710_12004393", "732", "500000", "1 189287;2 10", 90, "Rek:0215;terminal=1183;pdt=2010-07-14 12:03:49").DoRequest();
        //return new bl("check", "1183_140710_12004393", "732", "500000", "1 100088837;2 10", 90, "Rek:0215;terminal=1183;pdt=2010-07-14 12:03:49").DoRequest();
    }

}
