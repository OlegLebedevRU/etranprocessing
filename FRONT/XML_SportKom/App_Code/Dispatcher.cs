using System;
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





namespace XML_RyazanService
{
    /// <summary>
    /// Диспетчер сообщений Etran. Принимает и рассылает сообщения по
    /// рабочим серверам.
    /// </summary>
    public class Dispatcher : IHttpHandler
    {
        private const int RESPONSE_CODE_PAGE = 1251;

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
        /// <param name="Context">Текущий HTTP-контекст.</param>
        public void ProcessRequest(HttpContext Context)
        {
            try
            {
                GlobalObjectsManager.Logger.Info(Context.Request.RawUrl);
                Context.Response.Charset = "windows-1251";
                Context.Response.ContentEncoding = Encoding.GetEncoding("windows-1251");
                //Context.Response.ContentType = "text/xml";


                //Context.Response.Cache.SetNoServerCaching();
                //Context.Response.Cache.SetCacheability(HttpCacheability.NoCache);
                //Context.Response.Cache.SetAllowResponseInBrowserHistory(false);

                //string hhh = "org_id=85&from=2010-04-12&to=2010-04-13&okato=18810807140011000110";
                //string hhh = "org_id=1&from=2010-04-12&to=2010-08-13";
                //System.Collections.Specialized.NameValueCollection col = HttpUtility.ParseQueryString(hhh, Encoding.GetEncoding(1251));
                NameValueCollection col = Context.Request.QueryString;
                //NameValueCollection col = new NameValueCollection();
                //col.Add("type", "penalty");
                ////col.Add("type", "payments");
                //col.Add("from", "2010-08-01");
                //col.Add("to", "2012-11-22");
                //col.Add("org_id", "1");
                //col.Add("function", "Reference");


                Context.Response.ContentType = "text/xml";
                Context.Response.ContentType = "application/octet-stream";

                DateTime dt_from = DateTime.Now;
                DateTime dt_to = DateTime.Now;
                string Reference = col["Reference"];
                string file_name = "SportKom_";
                string sp_name = "SportKom_GetPayments";
                //if(Reference!=null && Reference.Length>0)

                int terminalNo = 0;
                int org_id = 0;
                if (col["TerminalNo"] != null)
                {
                    terminalNo = int.Parse(col["TerminalNo"]);
                }
                if (col["orgid"] != null)
                {
                    org_id = int.Parse(col["orgid"]);
                }
                dt_from = DateTime.Parse(col["from"]);
                dt_to = DateTime.Parse(col["to"]);
                file_name += dt_from.ToString("yyyyMMdd") + "-" + dt_to.ToString("yyyyMMdd") + ".xml";

                Context.Response.AddHeader("Content-Disposition", "attachment; filename=" + file_name);

                DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
                DataSet ds = (DataSet)db.Execute(sp_name, CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null, org_id, dt_from, dt_to, terminalNo);
                XmlDocument xml_doc = new XmlDocument();
                xml_doc.LoadXml(GetRootXml());
                XmlNode node = xml_doc.SelectSingleNode("/ROWSET");
                for (int i = 0; i < ds.Tables[0].Rows.Count; i++)
                {
                    NameValueCollection htbl = new NameValueCollection();
                    htbl.Add("COUNT", (i + 1).ToString());

                    for (int j = 0; j < ds.Tables[0].Columns.Count; j++)
                    {
                        string key = ds.Tables[0].Columns[j].ColumnName;
                        string val = ds.Tables[0].Rows[i][j].ToString();
                        htbl.Add(key, val);
                    }

                    ////XmlElement Row = XmlClass.CreateElement(xml_doc, "ROW", no);
                    XmlElement Row = xml_doc.CreateElement("ROW");
                    node.AppendChild(Row);
                    XmlClass.InsertInnerText(xml_doc, Row, htbl);
                }
                Context.Response.Write(xml_doc.OuterXml);
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
}
