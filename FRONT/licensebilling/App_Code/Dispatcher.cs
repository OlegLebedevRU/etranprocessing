using System.Text;
using System;
using System.Collections.Specialized;
using System.Data;
using System.Web;
using System.Xml;
using EtranLib.Data;
using System.Collections;

namespace Dispatcher
{


    /// <summary>
    /// Etran message dispatcher. Accepts and forwards messages to the
    /// worker servers.
    /// </summary>
    public class Dispatcher : IHttpHandler
    {

        public Dispatcher()
        {
        }

        #region IHttpHandler Members
        static public readonly string AppPath = HttpContext.Current.Server.MapPath("~/");


        private string GetRootXml()
        {
            return ("<?xml version='1.0' encoding='UTF-8'?><Response/>");
        }

        /// <summary>
        /// Handles the HTTP request for the payment system.
        /// </summary>
        /// <param name="Context">Current HTTP context.</param>
        public void ProcessRequest(HttpContext Context)
        {
            XmlDocument xml_doc = new XmlDocument();
            xml_doc.LoadXml(GetRootXml());
            XmlNode node = xml_doc.SelectSingleNode("/Response");
            bool error = false;
            try
            {
                GlobalObjectsManager.Logger.Info(Context.Request.RawUrl);
                Context.Response.Charset = "UTF-8";
                Context.Response.ContentEncoding = Encoding.UTF8;
                Context.Response.ContentType = "text/xml";


                //Context.Response.Cache.SetNoServerCaching();
                //Context.Response.Cache.SetCacheability(HttpCacheability.NoCache);
                //Context.Response.Cache.SetAllowResponseInBrowserHistory(false);

                //NameValueCollection col = Context.Request.QueryString;
                //Context.Response.ContentType = "text/xml";
                //Context.Response.ContentType = "application/octet-stream";


                string postParams = null;
                NameValueCollection qparams;
                NameValueCollection getQparams = HttpUtility.ParseQueryString(Context.Request.Url.Query);
                if (Context.Request.HttpMethod.ToUpper() == "POST")
                {

                    System.IO.Stream body = Context.Request.InputStream;
                    //System.Text.Encoding encoding = Context.Request.ContentEncoding;
                    //System.IO.StreamReader reader = new System.IO.StreamReader(body, encoding);
                    System.IO.StreamReader reader = new System.IO.StreamReader(body, Encoding.UTF8);
                    postParams = reader.ReadToEnd();
                    body.Close();
                    reader.Close();

                    GlobalObjectsManager.Logger.Info("POST: " + postParams);
                    //qparams = HttpUtility.ParseQueryString(PostParams, Encoding.GetEncoding(1251));
                    //HttpUtility.UrlEncode(
                    qparams = HttpUtility.ParseQueryString(postParams, Encoding.UTF8);
                }
                else
                {
                    //qparams = HttpUtility.ParseQueryString(Context.Request.Url.Query, Encoding.GetEncoding(1251));
                    //qparams = HttpUtility.ParseQueryString(Context.Request.Url.Query, Encoding.UTF8);
                    qparams = HttpUtility.ParseQueryString(Context.Request.Url.Query);
                }

                string SerialNumber = ClientCertHelper.GetDBSerialNumber(Context);
                //string SerialNumber = "8171";
GlobalObjectsManager.Logger.Info("X-Client-Cert-Serial: " + Context.Request.Headers["X-Client-Cert-Serial"]);
		GlobalObjectsManager.Logger.Info("=============");
                GlobalObjectsManager.Logger.Info("SerialNumber: " + SerialNumber);
		GlobalObjectsManager.Logger.Info("=============");

                string function = qparams["function"] ?? getQparams["function"];
                //function = "check";

                if (function.ToLower() == "check")
                {
                    GlobalObjectsManager.Logger.Info("hash:" + qparams["hash"]);
                    GlobalObjectsManager.Logger.Info("info:" + qparams["info"]);

                    var to = qparams["hash"];//.Base64Decode();
                    var info = qparams["info"];//.Base64Decode();

                    DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
                    Hashtable ht = (Hashtable)db.Execute("licensebilling_check", CommandType.StoredProcedure, DBManager.DataReadType.Hashtable,
                        null, SerialNumber);
                    var state = int .Parse(ht["state"].ToString());
                    var balance = ht["balance"];
                    GlobalObjectsManager.Logger.Info("state:" + state);
                    GlobalObjectsManager.Logger.Info("balance:" + balance);


                    XmlElement xml_balance = xml_doc.CreateElement("balance");
                    xml_balance.InnerText = balance.ToString();

                    XmlElement xml_state = xml_doc.CreateElement("state");
                    xml_state.InnerText = state == 0 ? "ok" : "error";
                    XmlElement result = xml_doc.CreateElement("Result");
                    result.InnerText = "OK";

                    node.AppendChild(result);
                    node.AppendChild(xml_state);
                    node.AppendChild(xml_balance);

                    Context.Response.Write(xml_doc.OuterXml);
                    GlobalObjectsManager.Logger.Info("DONE OK");
                }
                else
                {
                    XmlElement result = xml_doc.CreateElement("Result");
                    result.InnerText = "Error";
                    node.AppendChild(result);
                    Context.Response.Write(xml_doc.OuterXml);
                }
            }
            catch (Exception ex)
            {
                error = true;
                GlobalObjectsManager.Logger.Error(ex);
            }
            finally
            {
                if (error)
                {
                    XmlElement result = xml_doc.CreateElement("Result");
                    result.InnerText = "Error";
                    node.AppendChild(result);
                    Context.Response.Write(xml_doc.OuterXml);
                }
                GlobalObjectsManager.Logger.Info("REQUEST PROCESSING DONE");
            }
        }

        /// <summary>
        /// The handler is marked as reusable.
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

