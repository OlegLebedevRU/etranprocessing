using System;
using System.Web;
using System.Text;
using System.IO;
using System.Data;




namespace XEnrollService
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

        /// <summary>
        /// Обработчик HTTP-запроса пратежной систме.
        /// </summary>
        /// <param name="Context">Текущий HTTP-контекст.</param>
        public void ProcessRequest(HttpContext Context)
        {
            GlobalObjectsManager.Logger.Info(Context.Request.RawUrl);
            string response = string.Empty;
            string pkcs10 = string.Empty;
            string cpserial = string.Empty;
            string serial = string.Empty;
            string function = string.Empty;
            string pin = string.Empty;
            string tosign = string.Empty;
            string ca_serial = string.Empty;

            function = Context.Request.QueryString["function"];
            pin = Context.Request.QueryString["pin"];
            serial = Context.Request.QueryString["serial"];
            tosign = Context.Request.QueryString["tosign"];
            ca_serial = Context.Request.QueryString["ca_serial"];
            cpserial = Context.Request.QueryString["cpserial"];

            string login = Context.Request.QueryString["login"];
            string pwd = Context.Request.QueryString["pwd"];

            if (function.IndexOf("setup") > -1)
            {
                using (StreamReader sr = new StreamReader(Context.Request.InputStream))
                {
                    pkcs10 = sr.ReadToEnd();
                }
            }

            Context.Response.Charset = "windows-1251";
            Context.Response.ContentEncoding = Encoding.GetEncoding("windows-1251");
            Context.Response.ContentType = "text/xml";

            Context.Response.Cache.SetNoServerCaching();
            Context.Response.Cache.SetCacheability(HttpCacheability.NoCache);
            Context.Response.Cache.SetAllowResponseInBrowserHistory(false);

            try
            {

                if (function == "usercert_check")
                {
                }
                //usercert_setup

                if (EtranConfigurationManager.IsProxyCA)
                {
                    GlobalObjectsManager.Logger.Info("IsProxyCA");
                    if (function == "setup" && pkcs10 != null && pin != null && cpserial != null)
                    {
                        GlobalObjectsManager.Logger.Info("START SETUP PIN: " + pin + " pkcs10: " + pkcs10 + " cpserial:" + cpserial);
                        response = XEnrollClass.GetCertificateProxy(pin, pkcs10, cpserial, tosign);
                        GlobalObjectsManager.Logger.Info("RET: " + response);
                        Context.Response.Write(response);
                    }
                    else
                        if (function == "check" && pin != null)
                        {
                            GlobalObjectsManager.Logger.Info("check");
                            string req_params = Context.Request.Params["QUERY_STRING"];
                            string req_ret = XEnrollClass.NetReq(req_params);
                            Context.Response.Write(req_ret);
                        }
                }
                else
                {
                    if (function == "dbsetup" && pin != null && cpserial != null && serial != null && ca_serial != null)
                    {
                        GlobalObjectsManager.Logger.Info("START dbsetup PIN: " + pin);
                        string ret_db = XEnrollClass.DbSetup(pin, serial, cpserial, ca_serial);
                        GlobalObjectsManager.Logger.Info("RET: " + ret_db);
                        Context.Response.Write(ret_db);
                    }
                    else
                    if ( (function =="usercert_setup" || function == "setup") && pkcs10 != null && pin != null && cpserial != null)
                    {
                        GlobalObjectsManager.Logger.Info("START SETUP PIN: " + pin);
                        response = XEnrollClass.GetCertificate(login, pwd, pin, pkcs10, cpserial, tosign);

                        GlobalObjectsManager.Logger.Info("pin: " + pin + " RET: " + response);
                        GlobalObjectsManager.Logger.Info("RET: " + response);
                        Context.Response.Write(response);
                    }
                    else
                        if ( (function == "usercert_check" || function == "check") && pin != null)
                        {
                            GlobalObjectsManager.Logger.Info("check");
                            DataSet ds = null;
                            if(function == "usercert_check")
                                ds = DBInterface.UserAutho(login, pwd, pin);
                            else
                                ds = DBInterface.Autho(pin, tosign);

                            string result = (string)ds.Tables[0].Rows[0][0];
                            string dscr = (string)ds.Tables[0].Rows[0][1];
                            int code = 1;
                            if (ds.Tables[0].Rows[0]["code"] != null)
                                code = int.Parse(ds.Tables[0].Rows[0]["code"].ToString());

                            if (result == "ok")
                            {
                                GlobalObjectsManager.Logger.Info("ok");

                                string catype = (string)ds.Tables[0].Rows[0]["catype"];
                                string prov = (string)ds.Tables[0].Rows[0]["prov"];
                                string dn = (string)ds.Tables[0].Rows[0]["dn"];
                                string ret_pin = (string)ds.Tables[0].Rows[0]["pin"];
                                string ret_inner_xml;
                                ret_inner_xml = "<catype>" + catype + "</catype>";
                                ret_inner_xml += "<prov>" + prov + "</prov>";
                                ret_inner_xml += "<dn>" + dn + "</dn>";
                                ret_inner_xml += "<pin>" + ret_pin + "</pin>";
                                if (tosign != null)
                                    ret_inner_xml += "<sign>" + Signature.GetSign(tosign) + "</sign>";


                                GlobalObjectsManager.Logger.Info("RET: " + ret_inner_xml);
                                Context.Response.Write(XEnrollClass.MakeXmlResponse(XEnrollClass.eTypeResponses.OK, ret_inner_xml, 0));
                            }
                            else
                            {
                                GlobalObjectsManager.Logger.Info("RET: " + dscr);
                                Context.Response.Write(XEnrollClass.MakeXmlResponse(XEnrollClass.eTypeResponses.ERROR, dscr, code));
                            }
                        }
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("При проведении платежа возникла системная ошибка:", ex);
                Context.Response.Write(XEnrollClass.MakeXmlResponse(XEnrollClass.eTypeResponses.EXCEPTION, ex.Message, 1));
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
