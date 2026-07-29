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

        //авторизация по ЛС
        //return new bl("check", "1183_140710_12004393", "310", "100", "1 16211", 60, "0206;terminal=410;ps_id=14;pdt=2012-07-17 10:26:00", "0").DoRequest();
        //return new bl("check", "1183_140710_12004393", "310", "100", "1 216211", 60, "0206;terminal=410;ps_id=14;pdt=2012-07-17 10:26:00", "0").DoRequest();

        //авторизация по коду договора 
        //return new bl("check", "1183_140710_12004393", "310", "100", "1 0000019666;2 1", 60, "0206;terminal=410;ps_id=14;pdt=2012-07-17 10:26:00", "0").DoRequest();

        //Is=16211;phone=958201;id_pact=0000019666;amount=65.00;id_pact=0000044062;amount=100.00

        //1 16211;2 958201;3 0000019666:65.00&0000044062:100.00

        return new bl("check", "1183_140710_12004355", "310", "3000", "1 86C269CE7739BFC4F5529BF8590F0298EEF75725163E40;4 1", 90, "0001;org_id=1;terminal=9011;ps_id=14;pdt=2015-05-23 12:48:00;paym_id=103335668", "30").DoRequest();

    }
    
}
