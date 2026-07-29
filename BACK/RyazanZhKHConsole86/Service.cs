using System;

public class Service 
{
    public Service () {

        //Раскомментируйте следующую строку в случае использования сконструированных компонентов 
        //InitializeComponent(); 
    }



    public string DoRequest(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek)
    {
        return new bl(Function, PaymExtId, PaymSubjTp, Amount, Params, ConnectionTimeout, Rek).DoRequest();
    }

    static int intParse(string value)
    {
        value = value.Replace(",", "");
        int iVal = 0;
        bool result = int.TryParse(value, out iVal);
        return iVal;
    }


//2012-03-13 10:21:19,453 - param_in: Function: check PaymExtId: e7449ed2-7e76-4a5e-8977-25edc24469b8 PaymSubjTp:500 Amoun
//t: 10000 Params: ; ConnectionTimeout: 90 Rek:0206;terminal=410;ps_id=14;tsp_code=500
    public string Test()
    {
        string result = "EMPTY";
        try
        {
            //string in_info = "Mode=3;MonthPay=1102;Cnt1Vid=5;Cnt1Number=1;Cnt1Rash=555;Pay1Vid=2;Pay1Nac=20000;TotalPayment=850000";
            //string in_info = "Mode=1;MonthPay=1202;TotalPayment=1000";
            string in_info64 = "TW9kZT0xO01vbnRoUGF5PTEyMDI7UGF5MFZpZD0xO1BheTBTdW09MTAwO1BheTFWaWQ9NjtQYXkxU3VtPTEwMDtQYXkyVmlkPTc7UGF5MlN1bT0xMDA7UGF5M1ZpZD04O1BheTNTdW09MTAwO0NudDBWaWQ9MztDbnQwU3ViVmlkPTM7Q250ME51bWJlcj0wO0NudDBSYXNoPTE7Q250MlZpZD01O0NudDJTdWJWaWQ9NTtDbnQyTnVtYmVyPTE7Q250MlJhc2g9MTtDbnQzVmlkPTU7Q250M1N1YlZpZD01O0NudDNOdW1iZXI9MjtDbnQzUmFzaD0xO0NudDRWaWQ9NTtDbnQ0U3ViVmlkPTU7Q250NE51bWJlcj0zO0NudDRSYXNoPTE7Q250NVZpZD03O0NudDVTdWJWaWQ9NztDbnQ1TnVtYmVyPTE7Q250NVJhc2g9MTtDbnQ2VmlkPTc7Q250NlN1YlZpZD03O0NudDZOdW1iZXI9MjtDbnQ2UmFzaD0xO0NudDdWaWQ9NztDbnQ3U3ViVmlkPTc7Q250N051bWJlcj0zO0NudDdSYXNoPTE7Q250OFZpZD0xNztDbnQ4U3ViVmlkPTU7Q250OE51bWJlcj0wO0NudDhSYXNoPTM7Q250OVZpZD0xNztDbnQ5U3ViVmlkPTc7Q250OU51bWJlcj0wO0NudDlSYXNoPTM7VG90YWxQYXltZW50PTEwMDAw";
                //Encoding.base64Encode(in_info);
            //System.Diagnostics.Debug.WriteLine("in_info64 : " + in_info64);
            //in_info64 = "TW9kZT0yO01vbnRoUGF5PTExMDI7";
            //2012-03-12 11:39:44,953 - param_in: Function: payment PaymExtId: 08710076290311094809 PaymSubjTp:101 Amount: 9500 Params: 188 9608995441 ConnectionTimeout: 90 Rek:rek=9276465757:6a4392e437ee6cb7933f74f4a6f35c1;terminal=871;ps_id=11;pdt=2012-03-11 10:00:53

            //return in_info64;
            //result = new bl("payment", "PAN20100217-0001", "2003", "0", "1 21903300038001" + ";4 " + in_info64, 60, "rek=9276465757:6a4392e437ee6cb7933f74f4a6f35c1;terminal=871;ps_id=11;pdt=2012-03-11 10:00:53").DoRequest();
            //result = new bl("check", "PAN20100217-0001", "2003", "0", "1 10902000092036" + ";4 " + in_info64, 60, "terminal=410;ps_id=11;pdt=2012-03-11 10:00:53").DoRequest();
	    result = new bl("check", "8fe65ef3-6242-4d84-b645-09ea14086a7c", "500", "435000", "1 46002701088098;2 Новая улица, д.27а, кв.88;3 3;4 TW9kZT0yO01vbnRoUGF5PTE0MTI7Q250MFZpZD0zO0NudDBTdWJWaWQ9MztDbnQwTnVtYmVyPTA7Q250MFJhc2g9MTYzO0NudDBGWlM9O0NudDBFeHBDb25zPTtDbnQxVmlkPTU7Q250MVN1YlZpZD01O0NudDFOdW1iZXI9MDtDbnQxUmFzaD0zO0NudDFGWlM9O0NudDFFeHBDb25zPTtDbnQyVmlkPTc7Q250MlN1YlZpZD03O0NudDJOdW1iZXI9MDtDbnQyUmFzaD00O0NudDJGWlM9O0NudDJFeHBDb25zPTtDbnQzVmlkPTE3O0NudDNTdWJWaWQ9NTtDbnQzTnVtYmVyPTA7Q250M1Jhc2g9MztDbnQzRlpTPTtDbnQzRXhwQ29ucz07Q250NFZpZD0xNztDbnQ0U3ViVmlkPTc7Q250NE51bWJlcj0wO0NudDRSYXNoPTQ7Q250NEZaUz07Q250NEV4cENvbnM9O1RvdGFsUGF5bWVudD0w", 90, "Rek:0206;org_id=206;terminal=1187;ps_id=14;tsp_code=500").DoRequest();

            
            
             
        }
        catch (Exception e)
        {
            GlobalObjectsManager.Logger.Error(e);
            result = e.Message;
        }
        return result;
    }
    
}
