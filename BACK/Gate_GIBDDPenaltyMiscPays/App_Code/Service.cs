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


//2010-08-13 15:02:15,721 - https://etranprocessing.ru/payment/etran.ashx?function=payment&PaymExtId=0105_130810_14561450&PaymSubjTp=310&Amoun
//t=10000&Params=1+1;2+15IE123250;3+05.07.2009;4+100;5+90401360000;6+OAAIAA;7+?ONEAI;8+AEAIAAEIAE?;9+20.09.1977;10+18811630000010000140&TotalS
//um=100
//2010-08-13 15:02:15,737 - SerialNumber: 5149, Function: payment, PaymExtId: 0105_130810_14561450, PaymSubjTp:310, Amount: 10000, Params: 1 1;2 15ПК123250;3 05.07.2009;4 100;5 90401360000;6 ЦЕБОЕВ;7 РУСЛАН;8 АЛАНБЕКОВИЧ;9 20.09.1977;10 18811630000010000140, TotalSum: 100, Subject:
// C=ru, S=msk, L=moscow, O=33, OU=105, CN=002C043086, E=terminal@e-transfer.ru, Signature: EMPTY
//2010-08-13 15:02:15,768 - PaymExtId: 0105_130810_14561450 RET: <?xml version="1.0" encoding="windows-1251"?><Response><Result>OK</Result><Pa
//ymNumb>81130652</PaymNumb><PaymState>1</PaymState><PaymExtId>0105_130810_14561450</PaymExtId><Description>Платеж принят на обработку.</Descr
//iption></Response>
//2010-08-13 15:02:16,049 - https://etranprocessing.ru/payment/etran.ashx?function=payment&PaymExtId=0400_130810_19582084&PaymSubjTp=1010&Amou
//nt=9101&Params=1+9085924118&TotalSum=30

    [WebMethod]
    public string TEST()
    {
        return new bl("payment", "1183_140710_12004355", "310", "3000",
            "1 1;2 62АА365766;3 05.07.2009;4 50;5 90401360000;6 ЦЕБОЕВ;7 РУСЛАН;8 АЛАНБЕКОВИЧ;9 20.09.1977;10 18811630000010000140",
            90, "org_id=2", "30").DoRequest();
        //return new bl("check", "1183_140710_12004393", "310", "100", "2 62АА36576H", 60, "org_id=1", "0").DoRequest();

    }
    
}
