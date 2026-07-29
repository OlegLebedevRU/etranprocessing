using System;
using System.Globalization;
using System.Web;
using System.Text;
using System.IO;
using System.Data;
using EtranLib.Data;
using System.Data.SqlClient;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Xml;
using EtranLib.Xml;
using System.Linq;
using System.Diagnostics;

public class Translit
{
    private static readonly Dictionary<string, string> Words = new Dictionary<string, string>();

    static Translit()
    {
        Words.Add(" ", "");
        Words.Add("а", "a");
        Words.Add("б", "b");
        Words.Add("в", "v");
        Words.Add("г", "g");
        Words.Add("д", "d");
        Words.Add("е", "e");
        Words.Add("ё", "yo");
        Words.Add("ж", "zh");
        Words.Add("з", "z");
        Words.Add("и", "i");
        Words.Add("й", "j");
        Words.Add("к", "k");
        Words.Add("л", "l");
        Words.Add("м", "m");
        Words.Add("н", "n");
        Words.Add("о", "o");
        Words.Add("п", "p");
        Words.Add("р", "r");
        Words.Add("с", "s");
        Words.Add("т", "t");
        Words.Add("у", "u");
        Words.Add("ф", "f");
        Words.Add("х", "h");
        Words.Add("ц", "c");
        Words.Add("ч", "ch");
        Words.Add("ш", "sh");
        Words.Add("щ", "sch");
        Words.Add("ъ", "j");
        Words.Add("ы", "i");
        Words.Add("ь", "j");
        Words.Add("э", "e");
        Words.Add("ю", "yu");
        Words.Add("я", "ya");
        Words.Add("А", "A");
        Words.Add("Б", "B");
        Words.Add("В", "V");
        Words.Add("Г", "G");
        Words.Add("Д", "D");
        Words.Add("Е", "E");
        Words.Add("Ё", "Yo");
        Words.Add("Ж", "Zh");
        Words.Add("З", "Z");
        Words.Add("И", "I");
        Words.Add("Й", "J");
        Words.Add("К", "K");
        Words.Add("Л", "L");
        Words.Add("М", "M");
        Words.Add("Н", "N");
        Words.Add("О", "O");
        Words.Add("П", "P");
        Words.Add("Р", "R");
        Words.Add("С", "S");
        Words.Add("Т", "T");
        Words.Add("У", "U");
        Words.Add("Ф", "F");
        Words.Add("Х", "H");
        Words.Add("Ц", "C");
        Words.Add("Ч", "Ch");
        Words.Add("Ш", "Sh");
        Words.Add("Щ", "Sch");
        Words.Add("Ъ", "J");
        Words.Add("Ы", "I");
        Words.Add("Ь", "J");
        Words.Add("Э", "E");
        Words.Add("Ю", "Yu");
        Words.Add("Я", "Ya");
    }

    public static string DoIt(string source)
    {
        GlobalObjectsManager.Logger.Info("DoIt source " + source);
        string Return = string.Empty;
        try
        {
            foreach (char c in source)
            {
                GlobalObjectsManager.Logger.Info("DoIt source " + source + " char: " + c);
                string key = c.ToString(CultureInfo.InvariantCulture);
                Return += Words[key];
            }
        }
        catch (Exception ex)
        {

            //return Words.Aggregate(source, (current, pair) => current.Replace(pair.Key, pair.Value));
            return Return.TrimEnd(' ');
        }
        return Return;
    }

}

/// <summary>
/// Диспетчер сообщений Etran. Принимает и рассылает сообщения по
/// рабочим серверам.
/// </summary>
public class Dispatcher : IHttpHandler
{
    private const int ResponseCodePage = 1251;

    public Dispatcher()
    {
    }

    #region IHttpHandler Members


    private string GetRootXml()
    {
        return ("<?xml version='1.0' encoding='windows-1251'?><ROWSET/>");
    }
    public static IEnumerable<string> SplitByLength(string str, int maxLength)
    {
        for (int index = 0; index < str.Length; index += maxLength)
        {
            yield return str.Substring(index, Math.Min(maxLength, str.Length - index));
        }
    }
    /// <summary>
    /// Обработчик HTTP-запроса пратежной систме.
    /// </summary>
    /// <param name="context">Текущий HTTP-контекст.</param>
    public void ProcessRequest(HttpContext context)
    {
        try
        {
            GlobalObjectsManager.Logger.Info(context.Request.RawUrl);
            context.Response.Charset = "windows-1251";
            context.Response.ContentEncoding = Encoding.GetEncoding("windows-1251");
            //Context.Response.ContentType = "text/xml";


            //Context.Response.Cache.SetNoServerCaching();
            //Context.Response.Cache.SetCacheability(HttpCacheability.NoCache);
            //Context.Response.Cache.SetAllowResponseInBrowserHistory(false);

            //string hhh = "org_id=85&from=2010-04-12&to=2010-04-13&okato=18810807140011000110";
            //string hhh = "org_id=1&from=2010-04-12&to=2010-08-13";
            //System.Collections.Specialized.NameValueCollection col = HttpUtility.ParseQueryString(hhh, Encoding.GetEncoding(1251));
            NameValueCollection col = context.Request.QueryString;
            //NameValueCollection col = new NameValueCollection();
            //col.Add("type", "penalty");
            ////col.Add("type", "payments");
            //col.Add("from", "2010-08-01");
            //col.Add("to", "2012-11-22");
            //col.Add("org_id", "1");
            //col.Add("function", "Reference");


            context.Response.ContentType = "text/xml";
            context.Response.ContentType = "application/octet-stream";

            DateTime dt_from = DateTime.Now;
            DateTime dt_to = DateTime.Now;

            string sp_name = "PRTL_PV_GetPayments2";

            string terminalNo = "";

            if (col["TerminalNo"] != null)
            {
                terminalNo = col["TerminalNo"].Trim();
            }

            int org_id = 0;
            if (col["org_id"] != null)
            {
                org_id = int.Parse(col["org_id"]);
            }


            dt_from = DateTime.Parse(col["from"]);
            dt_to = DateTime.Parse(col["to"]);

            string fileName = "PAYM_" + dt_from.ToString("yyyyMMdd") + "-" + dt_to.ToString("yyyyMMdd") + ".txt";

            GlobalObjectsManager.Logger.Info("terminalNo " + terminalNo);

            context.Response.AddHeader("Content-Disposition", "attachment; filename=" + fileName);

//            @number varchar(50)= '',
//@datestart varchar(50),
//@datefin varchar(50),
//@termnumber_list varchar(8000)= '',
//@state int= 0,
//@tsp_code_list  varchar(1024) = '',
//@PaymExtId varchar(20)= '',
//@org_id int,
//@user_id int,
//@user_participiate int= 0,
//@psid int= 0,
//@pay_period int= 1,
//@serv_dt int= 1,
//@trans_start varchar(20) = '',
//@trans_fin varchar(20)= '',
//@top int = 3000

            DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
            DataSet ds = (DataSet)db.Execute(sp_name, CommandType.StoredProcedure
                , DBManager.DataReadType.DataSet, null,
                ""
                ,
                col["from"]
                ,
                col["to"]
                , 
                terminalNo
                ,
                0
                ,
                0
                ,
                ""
                ,
                ""
                ,
                org_id
                ,
                4
                ,
                0
                ,
                0
                ,
                1
                ,
                1
                ,
                ""
                ,
                ""
                ,
                10000
                );

            GlobalObjectsManager.Logger.Info("sp_name exec OK ");
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < ds.Tables[0].Rows.Count; i++)
            {
                string line = "";
                string tran = ds.Tables[0].Rows[i]["transaction"].ToString();
                string tnum = ds.Tables[0].Rows[i]["term_number"].ToString();
                string totalsum = ds.Tables[0].Rows[i]["total_sum"].ToString();
                string amount = ds.Tables[0].Rows[i]["payment_amount"].ToString();
                string paym_dt = ds.Tables[0].Rows[i]["paym_dt"].ToString();
                string paym_out_dt = ds.Tables[0].Rows[i]["paym_out_dt"].ToString();
                line = tran + ";" + tnum + ";" + totalsum + ";" + amount + ";" + paym_dt + ";" + paym_out_dt + ";";
                sb.AppendLine(line);
                string state = ds.Tables[0].Rows[i]["state"].ToString();
                sb.AppendLine(state);
                string tsp_name = ds.Tables[0].Rows[i]["tsp_name"].ToString();
                string tsp_par = ds.Tables[0].Rows[i]["tsp_par"].ToString();
                var s = SplitByLength(tsp_name + ";" + tsp_par, 80);
                foreach (var a in s)
                {
                    line = "\t\t" + a;
                    sb.AppendLine(line);
                }
                sb.AppendLine("---------------------------------");
            }
            context.Response.Write(sb.ToString());
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error(ex);
        }
    }

    /// <summary>
    /// Обработчик определяется как повторно используемый.
    /// </summary>
    public bool IsReusable
    {
        get
        {
            return true;
        }
    }

    #endregion
}