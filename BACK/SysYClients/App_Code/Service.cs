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
    //public string DoRequest()
    {
        return new bl(Function, PaymExtId, PaymSubjTp, Amount, Params, ConnectionTimeout, Rek, TotalSum).DoRequest();
        //2010-07-14 14:49:07,299 - param_in: Function: payment PaymExtId: 1183_140710_12004393 PaymSubjTp:374 Amount: 3500 Params: 9 9280694876;8 0;7 3500;6 ВЛАДИКАВКАЗСКАЯ 47 А КВ 13;5 ЛАМЕР;4 РАЙДЕР;3 ПРОЛОДЫРЬ;2 ООО ЗВЕЗДА;1 32110807020011000110 ConnectionTimeout: 90 Rek:0215;terminal=1183;pdt=2010-07-14 12:03:49
        //return new bl("payment", "1183_140710_12004393", "374", "3500", "9 9280694876;8 0;7 3500;6 ВЛАДИКАВКАЗСКАЯ 47 А КВ 13;5 ЛАМЕР;4 РАЙДЕР;3 ПРОЛОДЫРЬ;2 ООО ЗВЕЗДА;1 32110807020011000110",90,"Rek:0215;terminal=1183;pdt=2010-07-14 12:03:49","0").DoRequest();

    }

    [WebMethod]
    public long GetAmount(string Rek)
    {
        return new bl("GetAmount", "GetAmount", "1", "1", "1 1", 60, Rek, "0").GetAmount();
    }

    
    [WebMethod]
    public string TEST()
    {
        //return new bl("payment", "1183_140710_12004355", "310", "3000", "1 89099425434", 90, "0001;org_id=1;terminal=9011;ps_id=14;pdt=2015-05-23 12:48:00;paym_id=103335668", "30").DoRequest();
        return new bl("check", "1183_140710_12004355", "310", "3000", "1 79031346090", 90, "0001;org_id=1;terminal=9011;ps_id=14;pdt=2015-05-23 12:48:00;paym_id=103335668", "30").DoRequest();

    }

}
