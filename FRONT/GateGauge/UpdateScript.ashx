<%@ WebHandler Language="C#" Class="UpdateScript" %>

using System;
using System.Collections.Specialized;
using System.Globalization;
using System.IO;
using System.Text;
using System.Web;

public class UpdateScript : IHttpHandler {

    public static object lockThis = new object();

    public void ProcessRequest(HttpContext Context)
    {
        int KioskId = 0;
        try
        {
            GlobalObjectsManager.Logger.Info(Context.Request.Url);
            Context.Response.Charset = "utf-8";
            Context.Response.ContentEncoding = Encoding.UTF8;
            Context.Response.ContentType = "text/xml";
            Context.Response.Cache.SetNoServerCaching();
            Context.Response.Cache.SetCacheability(HttpCacheability.NoCache);
            Context.Response.Cache.SetAllowResponseInBrowserHistory(false);
            string query = (string)null;
            if (Context.Request.HttpMethod.ToUpper() == "POST")
            {
                Stream inputStream = Context.Request.InputStream;
                Encoding contentEncoding = Context.Request.ContentEncoding;
                StreamReader streamReader = new StreamReader(inputStream, contentEncoding);
                query = streamReader.ReadToEnd();
                inputStream.Close();
                streamReader.Close();
            }
            string Script = "";
            if (query != null)
            {
                NameValueCollection nameValueCollection = HttpUtility.ParseQueryString(query, Encoding.UTF8);
                int Number = int.Parse(nameValueCollection["sysUpdateCounter"]);
                string sysUpdateError = nameValueCollection["sysUpdateError"];
                GlobalObjectsManager.Logger.Info((object)string.Concat(new object[4]
          {
            (object) "UpdateScript KioskId ",
            (object) KioskId,
            (object) " sysUpdateCounter:",
            (object) Number
          }));
                GlobalObjectsManager.Logger.Info((object)string.Concat(new object[4]
          {
            (object) "UpdateScript KioskId ",
            (object) KioskId,
            (object) " sysUpdateError:",
            (object) sysUpdateError
          }));
                string serialNumber = Context.Request.ClientCertificate.SerialNumber;
                if (SqlSp.GetKioskId(int.Parse(serialNumber.Remove(0, serialNumber.Length - 11).Replace("-", ""), NumberStyles.HexNumber), out KioskId))
                {
                    GlobalObjectsManager.Logger.Info((object)("UpdateScript KioskId " + (object)KioskId + " try get script"));
                    if (SqlSp.GetKioskUpdateScript(KioskId, Number, sysUpdateError, out Script))
                    {
                        if (Script == null)
                            GlobalObjectsManager.Logger.Info((object)("UpdateScript KioskId " + (object)KioskId + " Скрипт null"));
                        else
                            GlobalObjectsManager.Logger.Info((object)string.Concat(new object[4]
                {
                  (object) "UpdateScript KioskId ",
                  (object) KioskId,
                  (object) " Got Script ",
                  (object) Script
                }));
                    }
                }
            }
            GlobalObjectsManager.Logger.Info((object)string.Concat(new object[4]
        {
          (object) "UpdateScript KioskId ",
          (object) KioskId,
          (object) " Отдаем:   ",
          (object) Script
        }));
            Context.Response.Write(Script);
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error((object)("UpdateScript KioskId " + (object)KioskId + " При обработке пакета возникла системная ошибка:"), ex);
            Context.Response.Write("");
        }
        finally
        {
        }
    }

    public bool IsReusable {
        get {
            return false;
        }
    }

}