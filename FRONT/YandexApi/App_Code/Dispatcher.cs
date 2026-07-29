using System.Text;
using System;
using System.Collections.Specialized;
using System.Web;
using System.Threading;
using Newtonsoft.Json.Linq;

namespace Dispatcher
{

    /// <summary>
    /// Диспетчер сообщений Etran. Принимает и рассылает сообщения по
    /// рабочим серверам.
    /// </summary>
    public class Dispatcher : IHttpHandler
    {

        #region IHttpHandler Members
        static public readonly string AppPath = HttpContext.Current.Server.MapPath("~/");

        /// <summary>
        /// Обработчик HTTP-запроса пратежной систме.
        /// </summary>
        /// <param name="Context">Текущий HTTP-контекст.</param>
        public void ProcessRequest(HttpContext Context)
        {
            bool error = false;
            try
            {
                GlobalObjectsManager.Logger.Info(Context.Request.RawUrl);
                Context.Response.ContentEncoding = Encoding.UTF8;
                Context.Response.ContentType = "text/plain";


                string postParams = null;
                NameValueCollection qparams;
                NameValueCollection getQparams = HttpUtility.ParseQueryString(Context.Request.Url.Query);
                if (Context.Request.HttpMethod.ToUpper() == "POST")
                {

                    System.IO.Stream body = Context.Request.InputStream;
                    System.IO.StreamReader reader = new System.IO.StreamReader(body, Encoding.UTF8);
                    postParams = reader.ReadToEnd();
                    body.Close();
                    reader.Close();
                    GlobalObjectsManager.Logger.Info("POST: " + postParams);
                    qparams = HttpUtility.ParseQueryString(postParams, Encoding.UTF8);
                }
                else
                {
                    qparams = HttpUtility.ParseQueryString(Context.Request.Url.Query);
                }

                string code = qparams["code"];
                string function = qparams["function"];
                int org_id = 0;
                int.TryParse(qparams["org_id"], out org_id);
                if (!string.IsNullOrEmpty(function) && function.ToLower() == "gettoken" && !string.IsNullOrEmpty(code) && org_id >0)
                {
                    new Thread(() => {
                        try
                        {
                            GlobalObjectsManager.Logger.Info("start Finished for org_id " + org_id);
                            var ya = new YandexSupport(org_id);
                            var token = ya.GetToken(code);
                            var jo = JObject.Parse(token);
                            var w = ya.ActivationDone(org_id, jo["access_token"].ToString());
                            GlobalObjectsManager.Logger.Info("Finished for wallet " + w);
                        }
                        catch(Exception ex)
                        {
                            GlobalObjectsManager.Logger.Error(ex);
                        }
                    }) { IsBackground = true }.Start();

                    Context.Response.Write("OK");
                    return;
                }
                Context.Response.Write("UNKNOWN REQUEST");
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
                    Context.Response.Write("ERROR");
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
