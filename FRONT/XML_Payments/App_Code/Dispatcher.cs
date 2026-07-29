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
    public static string HashString(string Value)
    {
        System.Security.Cryptography.MD5CryptoServiceProvider x = new System.Security.Cryptography.MD5CryptoServiceProvider();
        byte[] data = System.Text.Encoding.ASCII.GetBytes(Value);
        data = x.ComputeHash(data);
        string ret = "";
        for (int i = 0; i < data.Length; i++)
            ret += data[i].ToString("x2").ToLower();
        return ret;
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
            string login = col["login"].Trim();
            string pass = col["pass"].Trim();


            if (col["kiosknum"] != null)
            {
                terminalNo = col["kiosknum"].Trim();
            }

            int org_id = 0;
            if (col["Orgid"] != null)
            {
                org_id = int.Parse(col["Orgid"]);
            }

            DateTime date = DateTime.MinValue;
            if (col["date"] != null)
            {
                date = DateTime.Parse(col["date"]);
                dt_from = date;
                dt_to = date.AddDays(1);
            }
            else
            {
                dt_from = DateTime.Parse(col["from"]);
                dt_to = DateTime.Parse(col["to"]);
            }
            string fileName = "PAYM_" + dt_from.ToString("yyyyMMdd") + "-" + dt_to.ToString("yyyyMMdd") + ".xml";
            if (date != DateTime.MinValue)
                fileName = "PAYM_" + date.ToString("yyyyMMdd") + ".xml";

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
            GlobalObjectsManager.Logger.Info("login " + login);
            DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
            var user = (NameValueCollection)db.Execute("PRTL_GetUserByLogin", CommandType.StoredProcedure, DBManager.DataReadType.NameValueCollection, null, login);
            var md5pwd = user["md5pwd"];
            int userId = int.Parse(user["user_id"]);
            bool reestr = bool.Parse(user["reestr"]);
            GlobalObjectsManager.Logger.Info("userId " + userId + " reestr " + reestr);
            if (!reestr)
            {
                return;
            }
            var passmd5 = HashString(pass);
            if (md5pwd != passmd5)
            {
                GlobalObjectsManager.Logger.Info("Invalid pass.");
                return;
            }
            DataSet ds = (DataSet)db.Execute(sp_name, CommandType.StoredProcedure
            , DBManager.DataReadType.DataSet, null,
            ""
            ,
            dt_from.ToString("yyyy-MM-dd")
            ,
            dt_to.ToString("yyyy-MM-dd")
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
            userId
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

            //StringBuilder sb = new StringBuilder();
            XmlDocument xmlDoc = new XmlDocument();
            xmlDoc.LoadXml(GetRootXml());
            XmlNode node = xmlDoc.SelectSingleNode("/ROWSET");
            GlobalObjectsManager.Logger.Info("sp_name exec OK count " + ds.Tables[0].Rows.Count);

            for (int i = 0; i < ds.Tables[0].Rows.Count; i++)
            {
                List<KeyValuePair<string, string>> htbl = new List<KeyValuePair<string, string>>();
                for (int j = 0; j < ds.Tables[0].Columns.Count; j++)
                {
                    string key = ds.Tables[0].Columns[j].ColumnName;
                    string val = ds.Tables[0].Rows[i][j].ToString();
                    htbl.Add(new KeyValuePair<string, string>(key, val));
                }
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