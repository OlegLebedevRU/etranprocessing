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

            string sp_name = "RyazanUmMarsh_GetPayments";
            string pref = "UmMarsh_";
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
                List<KeyValuePair<string, string>> htbl = new List<KeyValuePair<string, string>>();
                htbl.Add(new KeyValuePair<string, string>("COUNT", (i + 1).ToString()));
                for (int j = 0; j < ds.Tables[0].Columns.Count; j++)
                {
                    string key = ds.Tables[0].Columns[j].ColumnName;
                    string val = ds.Tables[0].Rows[i][j].ToString();
                    if (key.ToLower() == "paym_param")
                    {
                        NameValueCollection paymParams = EtranLib.Collection.Collection.GetNameValueCollection(val);
                        foreach (string nvcKey in paymParams)
                        {
                            int tspParCode = int.Parse(nvcKey);
                            string tagName = "";
                            string tagBody = paymParams[nvcKey];

                            switch (tspParCode)
                            {
                                case 0:
                                    tagName = "PHONE_NUMBER";
                                    break;
                                case 1:
                                    tagName = "CARD_NUM";
                                    break;
                                case 2:
                                    tagName = "CARD_SERIES";
                                    break;
                                case 3:
                                    tagName = "PAYM_SUM";
                                    break;
                                case 4:
                                    tagName = "CARD_NUM_FULL";
                                    break;
                            }
                            htbl.Add(new KeyValuePair<string, string>(tagName, tagBody));
                        }
                    }
                    else
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