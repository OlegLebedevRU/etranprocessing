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
                //GlobalObjectsManager.Logger.Info("DoIt source " + source + " char: " + c);
                string key = c.ToString(CultureInfo.InvariantCulture);
                Return += Words[key];
            }
        }
        catch (Exception ex)
        {

            //return Words.Aggregate(source, (current, pair) => current.Replace(pair.Key, pair.Value));
            return Return.TrimEnd(' ');
        }
        GlobalObjectsManager.Logger.Info("DoIt result " + Return);
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


    static private Dictionary<int, string> dicKodUslugi = new Dictionary<int, string>()
    {
        { 1, "Оплата за обучение на ФДПО" }
        ,
        { 2, "Оплата за обучение в ординатуре" }
        ,
        { 3, "Оплата за обучение в аспирантуре" }
                ,
        { 4, "Оплата за обучение в магистратуре" }
                ,
        { 5, "Оплата за обучение в интернатуре" }
                ,
        { 6, "Оплата за обучение в медицинском классе" }
    };

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

            string sp_name = "RyazanMed_GetPayments";
            string pref = "MED_";

            //if (function.ToLower() == "dou")
            //{
            //    sp_name = "RyazanDOU_GetPayments";
            //    pref = "DOU_";
            //}
            //if (function.ToLower() == "zhkh")
            //{
            //    sp_name = "RyazanZhKH_GetPayments";
            //    pref = "ZHKH_";
            //}
            int terminalNo = 0;

            if (col["TerminalNo"] != null)
            {
                terminalNo = int.Parse(col["TerminalNo"]);
            }


            dt_from = DateTime.Parse(col["from"]);
            dt_to = DateTime.Parse(col["to"]);
            int org_id = int.Parse(col["org_id"] == null ? "0" : col["org_id"]);
            string fileName = pref + dt_from.ToString("yyyyMMdd") + "-" + dt_to.ToString("yyyyMMdd") + ".xml";

            GlobalObjectsManager.Logger.Info("terminalNo " + terminalNo);

            context.Response.AddHeader("Content-Disposition", "attachment; filename=" + fileName);

            DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
            DataSet ds = (DataSet)db.Execute(sp_name, CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null
                , org_id, dt_from, dt_to, terminalNo);

            GlobalObjectsManager.Logger.Info("sp_name exec OK ");

            XmlDocument xmlDoc = new XmlDocument();
            xmlDoc.LoadXml(GetRootXml());
            XmlNode node = xmlDoc.SelectSingleNode("/ROWSET");
            for (int i = 0; i < ds.Tables[0].Rows.Count; i++)
            {
                //NameValueCollection htbl = new NameValueCollection();
                List<KeyValuePair<string, string>> htbl = new List<KeyValuePair<string, string>>();
                int koduslugi = 0;
                string nazplat = "";
                htbl.Add(new KeyValuePair<string, string>("COUNT", (i + 1).ToString()));
                int tspCode = 0;
                for (int j = 0; j < ds.Tables[0].Columns.Count; j++)
                {
                    string key = ds.Tables[0].Columns[j].ColumnName;
                    string val = ds.Tables[0].Rows[i][j].ToString();
                    if (key.ToLower() == "paym_subjtp")
                    {
                        tspCode = int.Parse(val);
                        string tspName = GlobalObjectsManager.TspCodes[tspCode];
                        //string tagBody = "[Code=" + tspCode + "]" + tspName;
                        string tagBody = tspName;
                        tagBody = "УФК по Рязанской области (ФГБОУ ВО РязГМУ Минздрава России л/с 20596Х90310)";
                        htbl.Add(new KeyValuePair<string, string>(key, tagBody));
                        if (tspCode == 835)
                        {
                            nazplat = "";
                        }
                        else
                            nazplat = tspName;
                    }
                    else
                        if (key.ToLower() == "paym_param")
                    {
                        NameValueCollection paymParams = EtranLib.Collection.Collection.GetNameValueCollection(val);
                        foreach (string nvcKey in paymParams)
                        {
                            int tspParCode = int.Parse(nvcKey);
                            Dictionary<int, string> dicParams = GlobalObjectsManager.TspParams[tspCode];
                            foreach (KeyValuePair<int, string> pair in dicParams)
                            {
                                if (pair.Key == tspParCode)
                                {
                                    //string tagBody = "[Name=" + pair.Value + "]" + "[Code=" + pair.Key + "]" + paymParams[nvcKey];
                                    //htbl.Add(new KeyValuePair<string, string>(key, tagBody));

                                    string tagName = Translit.DoIt(pair.Value.ToLower()).ToUpper();
                                    string tagBody = paymParams[nvcKey];
                                    if(tagName.ToUpper() == "SUMMAKOPLATE")
                                    {
                                        tagBody = String.Format(CultureInfo.InvariantCulture, "{0:00.00}", double.Parse(tagBody)).Replace(".",",");
                                    }
                                    htbl.Add(new KeyValuePair<string, string>(tagName, tagBody));
                                    if (tagName == "KODUSLUGI")
                                    {
                                        koduslugi = 0;
                                        int.TryParse(tagBody, out koduslugi);
                                    }
                                }
                            }
                        }
                    }
                    else
                        htbl.Add(new KeyValuePair<string, string>(key, val));
                }

                if (tspCode == 835)
                {
                    if (dicKodUslugi.ContainsKey(koduslugi))
                        nazplat = dicKodUslugi[koduslugi];
                }

                htbl.Add(new KeyValuePair<string, string>("NAZPLAT", nazplat));


                ////XmlElement Row = XmlClass.CreateElement(xml_doc, "ROW", no);
                XmlElement Row = xmlDoc.CreateElement("ROW");
                node.AppendChild(Row);
                XmlClass.Insert(xmlDoc, Row, htbl);
            }
            context.Response.Write(xmlDoc.OuterXml);
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