using System;
using System.Globalization;
using System.Web;
using System.Text;
using System.Collections;
using System.Collections.Specialized;
using System.Security.Cryptography.X509Certificates;
using System.Security.Cryptography;
using System.Xml;

// https://etranprocessing.ru/payment/etran.ashx?function=payment&PaymExtId=20090806-0001&PaymSubjTp=1&Amount=500&Params=1 58573485793&TotalSum=0&PaymState=6
using log4net.Repository.Hierarchy;


namespace EtranDispatcher
{
    /// <summary>
    /// Диспетчер сообщений Etran. Принимает и рассылает сообщения по
    /// рабочим серверам.
    /// </summary>
    public class Dispatcher : IHttpHandler
    {
        private const int RESPONSE_CODE_PAGE = 1251;

        static public NameValueCollection GetNameValueCollection(string data, char delimiter)
        {
            string[] m = data.Split(delimiter);
            NameValueCollection result = new NameValueCollection();
            foreach (string s in m)
            {
                string[] m1 = s.Split('=');
                if (m1.Length != 2) continue;
                result.Add(m1[0].Trim(), m1[1]);
            }
            return result;
        }


        static public NameValueCollection GetNameValueCollection(string Params)
        {
            NameValueCollection paramsTable = new NameValueCollection();
            string[] paramEntries = Params.Split('&');
            if (paramEntries.Length > 1 || paramEntries[0] != string.Empty)
            {
                foreach (string param in paramEntries)
                {
                    int indx;
                    string key = string.Empty;
                    string val = string.Empty;

                    try
                    {
                        indx = param.IndexOf(' ');
                        if (indx < 0)
                            indx = param.IndexOf('=');

                        key = param.Substring(0, indx);
                        val = param.Substring(indx + 1, param.Length - indx - 1);
                    }
                    catch (Exception ex)
                    {
                        int h = 0;
                    }
                    paramsTable.Add(key, val);
                }
            }
            return paramsTable;
        }


        protected static bool isURLEncoded(string text)
        {
            if (text.IndexOf(' ') >= 0)
            {
                return false;
            }
            for (int i = 0; i < text.Length; i++)
            {
                char c = text[i];
                if (0x00 <= c && c <= 0x1f)
                {
                    return false;
                }
                if (c == 0x7F)
                {
                    return false;
                }
                if (0x80 <= c)
                {
                    return false;
                }
            }
            return true;
        }


        public string GetData(HttpContext Context)
        {
            string s = null;
            if (Context.Request.HttpMethod.ToUpper() == "POST")
            {
                System.IO.Stream body = Context.Request.InputStream;
                System.Text.Encoding encoding = Context.Request.ContentEncoding;
                System.IO.StreamReader reader = new System.IO.StreamReader(body, encoding);
                s = reader.ReadToEnd();
                body.Close();
                reader.Close();
            }
            return s;
        }

        static public string EncodeTo64(string toEncode)
        {
            byte[] toEncodeAsBytes
                  = System.Text.ASCIIEncoding.ASCII.GetBytes(toEncode);
            string returnValue
                  = System.Convert.ToBase64String(toEncodeAsBytes);
            return returnValue;
        }

        static public string DecodeFrom64(string encodedData)
        {
            byte[] encodedDataAsBytes
                = System.Convert.FromBase64String(encodedData);
            string returnValue =
               System.Text.ASCIIEncoding.ASCII.GetString(encodedDataAsBytes);
            return returnValue;
        }
        public static string MD5HashHex(string data)
        {
            byte[] result = (new MD5CryptoServiceProvider()).ComputeHash(Encoding.GetEncoding(1251).GetBytes(data));
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < result.Length; i++)
            {
                sb.Append(result[i].ToString("X2"));
            }
            return sb.ToString();
        }

        static string GetSign(string _tosign)
        {
            //GlobalObjectsManager.Logger.Info("tosign ...");
            //if (_tosign == null)
            //    throw new Exception("Не обнаружено исходной информации.");
            //else
            //    if (_tosign.Length < 1)
            //        throw new Exception("Не обнаружено исходной информации.");

            string tosign = DecodeFrom64(_tosign);
            string _tohash = tosign + EtranConfigurationManager.SignKey;
            GlobalObjectsManager.Logger.Info("_tohash: " + _tohash);
            string md5hash = MD5HashHex(_tohash);
            GlobalObjectsManager.Logger.Info("md5hash: " + md5hash);
            return md5hash;
            //string ret = XEnrollClass.MakeXmlResponse(XEnrollClass.eTypeResponses.OK, "<sign>" + md5hash + "</sign>");
            //GlobalObjectsManager.Logger.Info("ret: " + ret);
            //Context.Response.Write(ret);

        }

        public static int GetKopeks(string totalSum)
        {
            int result = 0;
            try
            {
                if (string.IsNullOrEmpty(totalSum))
                    return 0;

                if (totalSum.IndexOf(".", System.StringComparison.Ordinal) > -1 || totalSum.IndexOf(",", System.StringComparison.Ordinal) > -1)
                {
                    if (totalSum.IndexOf(".", System.StringComparison.Ordinal) > -1)
                        totalSum = totalSum.Replace(".", ",");

                    decimal decimalValue;
                    if (decimal.TryParse(totalSum, out decimalValue))
                    {
                        var integral = Decimal.Truncate(decimalValue);
                        result = (int)((decimalValue - integral) * 100);
                    }
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("GetKopeks", ex);
            }
            return result;
        }


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
            int org_id = 0;
            int PaymSubjTp;
            bool utf8 = false;
            string PaymExtId = "";
            GlobalObjectsManager.Logger.Info(Context.Request.UserHostAddress + " " + Context.Request.Url);
            //GlobalObjectsManager.Logger.Info("ContentEncoding.EncodingName: " + Context.Request.ContentEncoding.EncodingName);


            Context.Response.Charset = "windows-1251";
            //Context.Response.Charset = "utf-8";
            Context.Response.ContentEncoding = Encoding.GetEncoding("windows-1251");
            //Context.Response.ContentEncoding = Encoding.GetEncoding("utf-8");
            Context.Response.ContentType = "text/xml";
            Context.Response.Cache.SetNoServerCaching();
            Context.Response.Cache.SetCacheability(HttpCacheability.NoCache);
            Context.Response.Cache.SetAllowResponseInBrowserHistory(false);

            //string Function;
            //string Params;
            //string PaymSubjTp;
            //string Signature;
            //string PaymExtId;
            //string Amount;
            //string TotalSum;
            //string PaymentID;
            //string UserId;
            //string PaymState;

            string PostParams = "";
            NameValueCollection qparams;
            try
            {
                if (Context.Request.HttpMethod.ToUpper() == "POST")
                {

                    System.IO.Stream body = Context.Request.InputStream;
                    System.Text.Encoding encoding = Context.Request.ContentEncoding;
                    System.IO.StreamReader reader = new System.IO.StreamReader(body, encoding);
                    //System.IO.StreamReader reader = new System.IO.StreamReader(body, Encoding.UTF8);
                    PostParams = reader.ReadToEnd();
                    body.Close();
                    reader.Close();

                    GlobalObjectsManager.Logger.Info("encoding.CodePage: " + encoding.CodePage);
                    GlobalObjectsManager.Logger.Info("POST: " + PostParams);
                    //qparams = HttpUtility.ParseQueryString(PostParams, Encoding.GetEncoding(1251));
                    if (string.IsNullOrEmpty(EtranConfigurationManager.InComeEncodingName))
                        qparams = HttpUtility.ParseQueryString(PostParams, Encoding.Default);
                    else
                        qparams = HttpUtility.ParseQueryString(PostParams, Encoding.GetEncoding(EtranConfigurationManager.InComeEncodingName));

                    //if (qparams["Function"] != null && qparams["Function"].ToLower() == "addparams")
                    //{
                    //    qparams = HttpUtility.ParseQueryString(PostParams, Encoding.UTF8);
                    //}
                    //HttpUtility.UrlEncode(
                    //qparams = HttpUtility.ParseQueryString(PostParams, Encoding.UTF8);
                }
                else
                {
                    //qparams = HttpUtility.ParseQueryString(Context.Request.Url.Query, Encoding.GetEncoding(1251));
                    if (string.IsNullOrEmpty(EtranConfigurationManager.InComeEncodingName))
                        qparams = HttpUtility.ParseQueryString(Context.Request.Url.Query, Encoding.Default);
                    else
                        qparams = HttpUtility.ParseQueryString(Context.Request.Url.Query, Encoding.GetEncoding(EtranConfigurationManager.InComeEncodingName));
                }

                PaymSubjTp = int.Parse(qparams["PaymSubjTp"]);
                if (EtranConfigurationManager.IsInUtf8(PaymSubjTp))
                {
                    qparams = HttpUtility.ParseQueryString(PostParams, Encoding.UTF8);
                }


                string Function = qparams["Function"];
                string Params = qparams["Params"];

                if (string.IsNullOrEmpty(Params))
                {
                    Params = "1 " + EtranConfigurationManager.AddDefaultParams;
                }

                utf8 = EtranConfigurationManager.UTF8(PaymSubjTp.ToString());
                if (utf8)
                {
                    GlobalObjectsManager.Logger.Info("UTF8 FOR " + PaymSubjTp);
                    Context.Response.Charset = "utf-8";
                    Context.Response.ContentEncoding = Encoding.GetEncoding("utf-8");
                }


                string Signature = qparams["Signature"];
                PaymExtId = qparams["PaymExtId"];

                string Amount = qparams["Amount"];
                string TotalSum = qparams["TotalSum"];
                string PaymentID = qparams["PaymNumb"];
                string UserId = qparams["userid"];
                string PaymState = qparams["PaymState"];
                string PaymNumb = qparams["PaymNumb"];

                //try
                //{
                //    if (PaymSubjTp == "310")
                //    {
                //        if (Params.LastIndexOf(";9 ") == -1)
                //        {
                //            int ndx = Params.LastIndexOf(";7 ");
                //            if (ndx > 0)
                //            {
                //                Params = Params.Insert(ndx, "*");
                //                Params = Params.Replace("*;7 ", ";9 ");
                //            }
                //        }
                //    }
                //}
                //catch { }


                if (Function.ToLower() == "check" || Function.ToLower() == "payment" || Function.ToLower() == "checkfull")
                {
                }
                else
                    if (Function.ToLower() == "update" || Function.ToLower() == "addparams")
                {
                }
                else
                {
                    Context.Response.Write("<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>OK</Result><PaymExtId>");
                    Context.Response.Write(PaymExtId);
                    Context.Response.Write("</PaymExtId><Description>Функция не поддерживается.</Description></Response>");
                    return;
                }

                if (Function.ToLower() == "payment" && PaymExtId.Length > 20)
                    throw new Exception("PaymExtId invalid");



                //bool SignVerified = true;
                if (Signature == null || Signature == string.Empty)
                {
                    Signature = "EMPTY";
                }
                else
                {

                    GlobalObjectsManager.Logger.Info("Signature START...");
                    try
                    {
                        NameValueCollection subjectcollection = GetNameValueCollection(Context.Request.ClientCertificate.Subject, ',');
                        string CN = subjectcollection["CN"];

                        int.TryParse(subjectcollection["O"], out org_id);

                        //int org_ig = int.Parse(subjectcollection["O"]);
                        //org_ig = org_ig << 16;

                        //int org_ig = 1 << 16;

                        string sign = GetSign(Signature);
                        GlobalObjectsManager.Logger.Info("sign: " + sign + " CN: " + CN);
                        Signature = (sign == CN) ? "OK" : "ERROR";

                        //qparams.Remove("Signature");
                        //qparams.Remove("UserId");

                        //string msg = HttpUtility.UrlDecode(qparams.ToString());
                        //GlobalObjectsManager.Logger.Info("msg: " + msg);
                        //Signature = Signature.Replace(' ', '+');
                        //SignVerified = EtranProcessing.EtranCrypto.VerifyHash(msg, Signature, org_ig.ToString());
                        //Signature = (SignVerified) ? "OK" : "ERROR";
                    }
                    catch (Exception ex)
                    {
                        GlobalObjectsManager.Logger.Error("Signature", ex);
                        Signature = "ERROR";
                    }
                }

                string Subject = Context.Request.ClientCertificate.Subject;
                string SerialNumber = Context.Request.ClientCertificate.SerialNumber;
                SerialNumber = SerialNumber.Remove(0, SerialNumber.Length - 11).Replace("-", "");
                SerialNumber = int.Parse(SerialNumber, System.Globalization.NumberStyles.HexNumber).ToString();

                //string Subject = "Subject";
                //string SerialNumber = "2214";

                TotalSum = (TotalSum == null) ? "0" : TotalSum;

                int payTypeId = qparams["payTypeId"] == null ? 0 : int.Parse(qparams["payTypeId"]);
                var kopeks = GetKopeks(TotalSum);
                GlobalObjectsManager.Logger.Info("SerialNumber: " + SerialNumber + ", Function: " + Function + ", PaymExtId: " + PaymExtId + ", PaymSubjTp:" + PaymSubjTp + ", Amount: " + Amount + ", Params: " + Params + ", TotalSum: " + TotalSum + ", kopeks " + kopeks + ", Subject: " + Subject + ", Signature: " + Signature + " payTypeId: " + payTypeId);
                try
                {
                    if (TotalSum.IndexOf(",", System.StringComparison.Ordinal) > -1)
                        TotalSum = TotalSum.Remove(TotalSum.IndexOf(",", System.StringComparison.Ordinal));
                    if (TotalSum.IndexOf(".", System.StringComparison.Ordinal) > -1)
                        TotalSum = TotalSum.Remove(TotalSum.IndexOf(".", System.StringComparison.Ordinal));

                }
                catch (Exception ex)
                {
                    GlobalObjectsManager.Logger.Error(ex);
                }

                GlobalObjectsManager.Logger.Info(" PaymSubjTp: " + PaymSubjTp + " TotalSum: " + TotalSum);


                MessageProcessor processor = new MessageProcessor(EtranConfigurationManager.MessageProcessor);

                string result = null;
                if (Function.ToLower() == "checkfull")
                    result = processor.XmlPaymentInfo(PaymExtId, int.Parse(SerialNumber));
                else
                    if (Function.ToLower() == "addparams")
                    result = processor.UpdatePayment("0", "addparams", PaymExtId, PaymSubjTp.ToString(), Amount, Params, SerialNumber, TotalSum, Signature, "0");
                else
                    if (Function.ToLower() == "update")
                    result = processor.UpdatePayment(PaymentID, "payment", PaymExtId, PaymSubjTp.ToString(), Amount, Params, SerialNumber, TotalSum, Signature, UserId);
                else
                        //if ((PaymNumb != null && PaymNumb.Length > 0) && Function.ToLower() == "check")
                        //{
                        //    //result = processor.ProcessMessage(Function, PaymNumb);
                        //}
                        //else
                        if (Function.ToLower() == "payment" || Function.ToLower() == "check")
                    result = processor.ProcessMessage(Function, PaymExtId, PaymSubjTp.ToString(), Amount, Params, SerialNumber, TotalSum, Signature, UserId, PaymState, kopeks.ToString(CultureInfo.InvariantCulture), payTypeId.ToString());


                //if (Function.ToLower() == "payment" && PaymSubjTp == "1003")
                //{
                //    try
                //    {
                //        GlobalObjectsManager.Logger.Info("TRY-PARSE");
                //        string hh = PaymExtId;
                //        string[] dd = hh.Split('_');
                //        string date = dd[1];

                //        DateTime dt = DateTime.ParseExact(date, "ddMMyy", null);
                //        if (dt > DateTime.Now.AddYears(-1) && dt < DateTime.Now.AddDays(-1))
                //        {
                //            XmlDocument doc = new XmlDocument();
                //            doc.LoadXml(result);
                //            string res = doc.SelectSingleNode("Response/Result").InnerText;
                //            if (res.ToLower() == "error")
                //            {
                //                doc.SelectSingleNode("Response/Result").InnerText = "OK";
                //                result = doc.OuterXml;
                //            }
                //        }
                //    }
                //    catch (Exception ex) { GlobalObjectsManager.Logger.Error(result,ex); }
                //}

                if (utf8)
                {
                    result = result.Replace("windows-1251", "utf-8");
                }

                GlobalObjectsManager.Logger.Info("PaymExtId: " + PaymExtId + " RET: " + result);
                Context.Response.Write(result);

                processor.Dispose();

            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("При проведении платежа возникла системная ошибка:", ex);
                if (utf8)
                    Context.Response.Write("<?xml version = \"1.0\" encoding = \"utf-8\"?><Response><Result>Error</Result><PaymExtId>");
                else
                    Context.Response.Write("<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>Error</Result><PaymExtId>");

                Context.Response.Write(PaymExtId);
                Context.Response.Write("</PaymExtId><Description>При проведении платежа возникла системная ошибка: ");
                Context.Response.Write(ex.Message);
                Context.Response.Write("</Description></Response>");
            }
            finally
            {


                //if (org_id == 223)
                //{
                //    GlobalObjectsManager.Logger.Info("org_id == 223");
                //    Context.Response.Charset = "utf-8";
                //    Context.Response.ContentEncoding = Encoding.GetEncoding("utf-8");
                //}

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
