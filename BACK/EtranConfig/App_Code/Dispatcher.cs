using System;
using System.Web;
using System.Text;
using System.IO;
using System.Data;
using System.Configuration;
using App_Code;


namespace EtranConfig
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
                Context.Response.ContentType = "text/xml";


                Context.Response.Cache.SetNoServerCaching();
                Context.Response.Cache.SetCacheability(HttpCacheability.NoCache);
                Context.Response.Cache.SetAllowResponseInBrowserHistory(false);

                //System.Collections.Specialized.NameValueCollection col = HttpUtility.ParseQueryString("function=sms&service=balance", Encoding.UTF8);
                System.Collections.Specialized.NameValueCollection col = HttpUtility.ParseQueryString(Context.Request.Url.Query, Encoding.UTF8);
            

                
                string function = col["function"];
                switch (function)
                {
                    case"dbconn" :
                        string dbname = col["dbname"];
                        if (dbname != null)
                            Context.Response.Write(ConfigurationManager.ConnectionStrings[dbname].ConnectionString);
                        break;
                    case "sms":
                        string phone = col["phone"];
                        string msg = col["msg"];
                        string smsProvider = col["provider"]??"smsm";
                        string sign = col["sign"] ?? string.Empty;
                        string service = col["service"];
                        bool balance = false;
                        if (service != null && service.ToLower() == "balance")
                            balance = true;
			            smsProvider = "smstraffic";
                        string response=string.Empty;
                        if (smsProvider=="smsm")
                        {
                            response = SMS.Send(phone, msg, balance);
                        }
                        else
                        {
                            response = SMS.Send(phone, msg, balance, smsProvider, sign);
                        }
                        Context.Response.Write(response);
                        break;
                    case "email":
                        string address = col["address"];
                        string message = col["message"];
                        string subject = col["subject"];
                        Context.Response.Write(Email.Send(address, message, subject));
                        break;
                    case "getrek":
                        string serial_number = col["serial_number"];
                        string tsp_code = col["tsp_code"];
                        string paym_amount = col["paym_amount"];
                        Context.Response.Write(GetRek.getRek(serial_number, tsp_code, paym_amount));
                        break;
                }
               /* if (function == "dbconn")
                {
                    string dbname = col["dbname"];
                    if(dbname!=null)
                        Context.Response.Write(ConfigurationManager.ConnectionStrings[dbname].ConnectionString);
                    
                }
                else
                    if (function == "sms")
                    {
                        string phone = col["phone"];
                        string msg = col["msg"];
                        string service = col["service"];
                        bool balance = false;
                        if (service != null && service.ToLower() == "balance")
                            balance = true;

                        string ret = SMS.Send(phone, msg, balance);

                        Context.Response.Write(ret);
                    }
                    else
                        if (function == "getrek")
                        {
                            string serial_number = col["serial_number"];
                            string tsp_code = col["tsp_code"];
                            string paym_amount = col["paym_amount"];
                            string ret = GetRek.getRek(serial_number, tsp_code, paym_amount);
                            Context.Response.Write(ret);
                        }
                */
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
