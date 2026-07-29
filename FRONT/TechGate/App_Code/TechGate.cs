// Decompiled with JetBrains decompiler
// Type: TechGate.TechGate
// Assembly: TechGate, Version=1.0.4724.32104, Culture=neutral, PublicKeyToken=null
// MVID: 7D25CA8F-6EB1-4AE3-AFB7-31AA5758287A
// Assembly location: C:\PlaterraSvn\PROCESSING\FRONT\TechGate\bin\TechGate.dll

using System;
using System.Collections.Specialized;
using System.IO;
using System.Text;
using System.Web;

namespace TechGate
{
    public class TechGate : IHttpHandler
    {
        private const int RESPONSE_CODE_PAGE = 1251;

        public bool IsReusable
        {
            get
            {
                return true;
            }
        }

        public void ProcessRequest(HttpContext Context)
        {
            GlobalObjectsManager.Logger.Info((object)("URL: " + Context.Request.Url.AbsoluteUri));
            string s = (string)null;
            Context.Response.Charset = "windows-1251";
            Context.Response.ContentEncoding = Encoding.GetEncoding("windows-1251");
            Context.Response.ContentType = "text/xml";
            Context.Response.Cache.SetNoServerCaching();
            Context.Response.Cache.SetCacheability(HttpCacheability.NoCache);
            Context.Response.Cache.SetAllowResponseInBrowserHistory(false);
            string str1 = (string)null;
            NameValueCollection nameValueCollection;
            if (Context.Request.HttpMethod.ToUpper() == "POST")
            {
                Stream inputStream = Context.Request.InputStream;
                Encoding contentEncoding = Context.Request.ContentEncoding;
                StreamReader streamReader = new StreamReader(inputStream, contentEncoding);
                string query = streamReader.ReadToEnd();
                inputStream.Close();
                streamReader.Close();
                GlobalObjectsManager.Logger.Info((object)("POST: " + query));
                if (query.IndexOf("function=closeshift2") > -1)
                {
                    int p = query.IndexOf("personId=");
                    int p1 = query.IndexOf("&personId=");
                    if (p > 0 && p1 == -1)
                    {
                        query = query.Insert(p, "&");
                        GlobalObjectsManager.Logger.Info((object)("POST FIX: " + query));
                    }

                }
                nameValueCollection = HttpUtility.ParseQueryString(query, Encoding.GetEncoding(1251));
            }
            else
                nameValueCollection = HttpUtility.ParseQueryString(Context.Request.Url.Query, Encoding.GetEncoding(1251));

            //string query1 = @"function=inkass&InkassId=9&InkassExtId=0002_080416_13213901&PaymExtId=0002_080416_13204503&Note0=0&Note1=0&Note2=0&Note3=0&Note4=1&Note5=1&Note6=0&Note7=0&Note8=0&Note9=0&TotalNoteSum=600&TotalNoteCount=2&Coin0=0&Coin1=0&Coin2=0&Coin3=0&Coin4=0&Coin5=0&Coin6=0&Coin7=0&Coin8=0&Coin9=0&TotalCoinSum=0&TotalCoinCount=10&TotalSum=600&TotalCount=2&InkassDateTime=08.04.2016 13:21:40&Currency=1&TransactCount=2&TransactSum=600&LastSumInkass=300&cntInkass=36&cntInkassSum=161280&cntTransact=755&cntTotalSum=343318&cassetteNum=0&Inkassator=&Signature=TUI9MSBUbyBiZSBmaWxsZWQgYnkgTy5FLk0ufENQVT0xIEJGRUJGQkZGMDAwMzA2QTk=";
            //GlobalObjectsManager.Logger.Info((object)("POST: " + query1));
            //nameValueCollection = HttpUtility.ParseQueryString(query1, Encoding.GetEncoding(1251));

            try
            {
                string str2 = nameValueCollection["function"].ToLower();
                switch (str2)
                {
                    case "devicestatus":
                        DeviceStatus.ProcessMessage(nameValueCollection, ref Context);
                        break;
                    case "getshiftreport":
                    case "getinkassreport":
                        str1 = getshiftreport.ProcessMessage(nameValueCollection, ref Context);
                        break;
                    case "inkass":
                        InkassProcessor.ProcessMessage(nameValueCollection, ref Context);
                        break;
                    case "kiosk":
                        str1 = "<Kiosk24>" + Kiosk.CheckModel24(nameValueCollection) + "</Kiosk24>";
                        break;
                    case "tsplist":
                        str1 = "<TspList>" + Kiosk.GetTspList(nameValueCollection) + "</TspList>";
                        break;
                    case "closeshift":
                        closeshift.ProcessMessage(nameValueCollection, ref Context);
                        break;
                    case "closeshift2":
                        closeshift2.ProcessMessage(nameValueCollection, ref Context);
                        break;
                    default:
                        throw new Exception("function == " + str2 + " это значение не поддерживается");
                }
                GlobalObjectsManager.Logger.Info((object)(s + " RET: OK"));
                Context.Response.Write("<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>OK</Result><PaymExtId>");
                Context.Response.Write(s);
                Context.Response.Write("</PaymExtId><Description>1</Description>" + (str1 ?? "") + "</Response>");
            }
            catch (Exception ex)
            {
                if (ex.Message.IndexOf("duplicate") > -1)
                {
                    GlobalObjectsManager.Logger.Info((object)(s + " duplicate OK"));
                    Context.Response.Write("<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>OK</Result><PaymExtId>");
                    Context.Response.Write(s);
                    Context.Response.Write("</PaymExtId><Description>1</Description></Response>");
                }
                else
                {
                    GlobalObjectsManager.Logger.Error((object)"При проведении платежа возникла системная ошибка:", ex);
                    Context.Response.Write("<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>Error</Result><PaymExtId>");
                    Context.Response.Write(s);
                    Context.Response.Write("</PaymExtId><Description>При проведении платежа возникла системная ошибка: ");
                    Context.Response.Write(ex.Message);
                    Context.Response.Write("</Description></Response>");
                }
            }
        }
    }
}
