using System.Text;
using System;
using System.Collections.Specialized;
using System.Data;
using System.IO;
using System.Net;
using System.Web;
using System.Xml;


namespace Dispatcher
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
        static public readonly string AppPath = HttpContext.Current.Server.MapPath("~/");


        private string GetRootXml()
        {
            return ("<?xml version='1.0' encoding='windows-1251'?><Response/>");
        }

        /// <summary>
        /// Обработчик HTTP-запроса пратежной систме.
        /// </summary>
        /// <param name="Context">Текущий HTTP-контекст.</param>
        public void ProcessRequest(HttpContext Context)
        {
            XmlDocument xml_doc = new XmlDocument();
            xml_doc.LoadXml(GetRootXml());
            XmlNode node = xml_doc.SelectSingleNode("/Response");
            bool error = false;
            try
            {
                GlobalObjectsManager.Logger.Info(Context.Request.RawUrl);
                Context.Response.Charset = "windows-1251";
                Context.Response.ContentEncoding = System.Text.Encoding.GetEncoding("windows-1251");
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

                foreach (string key in qparams.AllKeys)
                {
                    XmlElement result = xml_doc.CreateElement(key);
                    result.InnerText = qparams[key];
                    node.AppendChild(result);
                }

                Context.Response.Write(xml_doc.OuterXml);
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
