using System;
using System.Globalization;
using System.Web;
using System.Text;
using System.Collections;
using System.Collections.Specialized;
using System.Collections.Generic;
using System.Security.Cryptography.X509Certificates;
using System.Security.Cryptography;
using System.Xml;
using System.Data;
using System.Data.SqlClient;

// https://etranprocessing.ru/payment/etran.ashx?function=payment&PaymExtId=20090806-0001&PaymSubjTp=1&Amount=500&Params=1 58573485793&TotalSum=0&PaymState=6
using log4net.Repository.Hierarchy;


namespace EtranDispatcher
{
    /// <summary>
    /// Etran message dispatcher. Accepts and forwards messages to the
    /// worker servers.
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
            //    throw new Exception("Source information not found.");
            //else
            //    if (_tosign.Length < 1)
            //        throw new Exception("Source information not found.");

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

        /// <summary>
        /// Parses the Params string ("1 value1;2 value2;3=value3") into a
        /// dictionary of {parameter_code: value}. Format matches
        /// RequestMessage.ParamsStringToHashtable from the shared EtranApi.
        /// </summary>
        private static Dictionary<int, string> ParsePaymentParams(string paramsStr)
        {
            var result = new Dictionary<int, string>();
            if (string.IsNullOrEmpty(paramsStr)) return result;

            foreach (string entry in paramsStr.Split(';'))
            {
                try
                {
                    int idx = entry.IndexOf('=');
                    if (idx < 0) idx = entry.IndexOf(' ');
                    if (idx < 0) continue;

                    string key = entry.Substring(0, idx).Trim();
                    string val = entry.Substring(idx + 1);

                    int parameterCode;
                    if (int.TryParse(key, out parameterCode))
                        result[parameterCode] = val;
                }
                catch (Exception ex)
                {
                    GlobalObjectsManager.Logger.Error("ParsePaymentParams: " + entry, ex);
                }
            }
            return result;
        }

        /// <summary>
        /// Looks up an existing payment by PaymExtId (matches the core
        /// query used by the legacy AModule_CheckPayment stored procedure,
        /// minus the external-forwarding fallback via GetRek_MP2/Url/Rek,
        /// which this simplified flow does not implement). Returns false
        /// if no matching payment exists.
        /// </summary>
        private static bool TryFindPayment(SqlConnection conn, string paymExtId, out int paymId, out int paymState)
        {
            paymId = 0;
            paymState = 0;
            using (SqlCommand cmd = new SqlCommand(
                "SELECT TOP 1 paym_id, paym_state FROM Payments WHERE PaymExtId = @PaymExtId", conn))
            {
                cmd.Parameters.AddWithValue("@PaymExtId", paymExtId);
                using (SqlDataReader reader = cmd.ExecuteReader())
                {
                    if (!reader.Read()) return false;
                    paymId = reader.GetInt32(0);
                    paymState = reader.GetInt16(1);
                    return true;
                }
            }
        }

        /// <summary>
        /// Simplified idempotent payment write directly to the Payments
        /// database, without calling the shared SOAP service
        /// MessageProcessor.asmx (which also queues the payment for
        /// operator/job review). Returns paym_id (existing one, if
        /// PaymExtId was already accepted before, or a new one).
        /// </summary>
        /// <summary>
        /// Stores payment params via the already existing idempotent
        /// stored procedure AModule_AddPaymentParam - it handles
        /// get-or-create of param_id via service..TspCodes/Parameter_codes.
        /// </summary>
        private static void AddPaymentParams(SqlConnection conn, int paymId, int paymSubjTp, string paramsStr)
        {
            foreach (var kv in ParsePaymentParams(paramsStr))
            {
                using (SqlCommand cmd = new SqlCommand("AModule_AddPaymentParam", conn))
                {
                    cmd.CommandType = CommandType.StoredProcedure;
                    cmd.Parameters.AddWithValue("@paym_id", paymId);
                    cmd.Parameters.AddWithValue("@paymsubjtp", paymSubjTp);
                    cmd.Parameters.AddWithValue("@Parameter_code", kv.Key);
                    cmd.Parameters.AddWithValue("@param_value", kv.Value);
                    cmd.ExecuteNonQuery();
                }
            }
        }

        private static int SimplifiedPutPayment(string paymExtId, int paymSubjTp, int amount, int serialNumber,
            int totalSum, string signature, int kopeks, int payTypeId, string paramsStr)
        {
            using (SqlConnection conn = new SqlConnection(GlobalObjectsManager.PaymentDbConnectionString))
            {
                conn.Open();

                int existingPaymId, existingPaymState;
                if (TryFindPayment(conn, paymExtId, out existingPaymId, out existingPaymState))
                    return existingPaymId;

                int paymId;
                using (SqlCommand cmd = new SqlCommand(
                    "INSERT INTO Payments (paym_datetime, paym_amount, PaymExtId, PaymSubjTp, paym_state, totalsum, serial_number, Signature, kopeks, payTypeId) " +
                    "OUTPUT INSERTED.paym_id " +
                    "VALUES (GETDATE(), @paym_amount, @PaymExtId, @PaymSubjTp, 2, @totalsum, @serial_number, @Signature, @kopeks, @payTypeId)", conn))
                {
                    cmd.Parameters.AddWithValue("@paym_amount", amount);
                    cmd.Parameters.AddWithValue("@PaymExtId", paymExtId);
                    cmd.Parameters.AddWithValue("@PaymSubjTp", paymSubjTp);
                    cmd.Parameters.AddWithValue("@totalsum", totalSum);
                    cmd.Parameters.AddWithValue("@serial_number", serialNumber);
                    cmd.Parameters.AddWithValue("@Signature", string.IsNullOrEmpty(signature) ? "EMPTY" : signature);
                    cmd.Parameters.AddWithValue("@kopeks", kopeks);
                    cmd.Parameters.AddWithValue("@payTypeId", payTypeId);
                    paymId = (int)cmd.ExecuteScalar();
                }

                AddPaymentParams(conn, paymId, paymSubjTp, paramsStr);

                return paymId;
            }
        }

        /// <summary>
        /// Real eligibility check for a serial+tsp_code combination, calling
        /// the existing read-only stored procedure service.dbo.GetRek_MP2
        /// directly (same one the legacy AModule_CheckPayment SP calls).
        /// Verifies the terminal's certificate is active, its organization
        /// is not locked/deleted, the TSP is active, the kiosk is allowed
        /// to sell this TSP, and a payment system/reward mapping exists.
        /// This call is read-only (no side effects), safe to run on every
        /// "check" request.
        /// </summary>
        private static bool TryCheckEligibility(SqlConnection conn, int serial, int tspCode, out string urlOut, out string rekOut, out int? psId, out string msg)
        {
            using (SqlCommand cmd = new SqlCommand("service.dbo.GetRek_MP2", conn))
            {
                cmd.CommandType = CommandType.StoredProcedure;
                cmd.Parameters.AddWithValue("@serial", serial);
                cmd.Parameters.AddWithValue("@tsp_code", tspCode);
                var pUrl = cmd.Parameters.Add("@url_out", SqlDbType.VarChar, 255);
                pUrl.Direction = ParameterDirection.Output;
                var pRek = cmd.Parameters.Add("@rek_out", SqlDbType.VarChar, 255);
                pRek.Direction = ParameterDirection.Output;
                var pPsId = cmd.Parameters.Add("@ps_id", SqlDbType.Int);
                pPsId.Direction = ParameterDirection.Output;
                var pMsg = cmd.Parameters.Add("@msg", SqlDbType.VarChar, 255);
                pMsg.Direction = ParameterDirection.Output;

                cmd.ExecuteNonQuery();

                urlOut = pUrl.Value as string;
                rekOut = pRek.Value as string;
                psId = (pPsId.Value == null || pPsId.Value == DBNull.Value) ? (int?)null : (int)pPsId.Value;
                msg = pMsg.Value as string;

                return !string.IsNullOrEmpty(urlOut) && !string.IsNullOrEmpty(rekOut) && psId.HasValue;
            }
        }

        /// <summary>
        /// Simplified equivalent of the legacy "check" function
        /// (AModule_CheckPayment): if a payment matching PaymExtId already
        /// exists, reports its real state (idempotent status check,
        /// matching legacy behavior - in practice this basically never
        /// happens, since terminals generate a fresh PaymExtId per
        /// transaction). Otherwise performs the real eligibility check
        /// (see TryCheckEligibility / GetRek_MP2) to verify whether the
        /// given terminal (serial) is currently authorized to submit a
        /// payment for this TSP, before the terminal proceeds to the main
        /// "payment" call. Does not implement the third-party payment
        /// gateway forwarding (Url/Rek external redirect) that the
        /// original stored procedure has - out of scope for this
        /// simplified flow.
        ///
        /// NOTE for the new (Python) processing stack: this eligibility
        /// check (service.Certificates/Kiosks/Organizations/TspCodes/
        /// TspKiosks/PayProperties/OrganizationReward/PaySystems, see
        /// GetRek_MP2) should be reimplemented there too - tracked as a
        /// separate follow-up task/session, not done here.
        /// </summary>
        private static string SimplifiedCheckPayment(string paymExtId, int serialNumber, int tspCode)
        {
            using (SqlConnection conn = new SqlConnection(GlobalObjectsManager.PaymentDbConnectionString))
            {
                conn.Open();

                int paymId, paymState;
                if (TryFindPayment(conn, paymExtId, out paymId, out paymState))
                    return BuildAckResponse(paymId, paymState, paymExtId, "Check passed.");

                string urlOut, rekOut, msg;
                int? psId;
                bool eligible = TryCheckEligibility(conn, serialNumber, tspCode, out urlOut, out rekOut, out psId, out msg);

                if (!string.IsNullOrEmpty(msg) && msg.Equals("Lock", StringComparison.OrdinalIgnoreCase))
                    return BuildErrorResponse(paymExtId, "Terminal or organization is locked.");

                if (!eligible)
                    return BuildErrorResponse(paymExtId, "Payment authentication error.");

                return BuildAckResponse(0, 0, paymExtId, "Check passed.");
            }
        }

        /// <summary>
        /// Simplified equivalent of the legacy "checkfull" function
        /// (MessageProcessor.XmlPaymentInfo): calls the same existing
        /// AModule_XmlPaymentInfo stored procedure directly (FOR XML
        /// EXPLICIT), returning its raw XML document as-is - same output
        /// shape as the original SOAP call, just without going through the
        /// shared MessageProcessor.asmx service. If no payment matches
        /// (the stored procedure's INNER JOINs return no rows), the result
        /// is an empty string, exactly as the previous SOAP-based call
        /// would also return for a non-existent payment.
        /// </summary>
        private static string SimplifiedXmlPaymentInfo(string paymExtId, int serialNumber)
        {
            using (SqlConnection conn = new SqlConnection(GlobalObjectsManager.PaymentDbConnectionString))
            {
                conn.Open();
                using (SqlCommand cmd = new SqlCommand("AModule_XmlPaymentInfo", conn))
                {
                    cmd.CommandType = CommandType.StoredProcedure;
                    cmd.Parameters.AddWithValue("@PaymExtId", paymExtId);
                    cmd.Parameters.AddWithValue("@serial_number", serialNumber);
                    using (SqlDataReader reader = cmd.ExecuteReader())
                    {
                        string ret = null;
                        while (reader.Read())
                        {
                            ret += reader.GetString(0);
                        }
                        return ret;
                    }
                }
            }
        }

        /// <summary>
        /// Simplified equivalent of the legacy "addparams"/"update"
        /// functions: requires the payment to already exist (found by
        /// PaymExtId), then stores the supplied params against it via the
        /// existing AModule_AddPaymentParam stored procedure. Returns an
        /// error if the payment does not exist yet, instead of silently
        /// acknowledging success without writing anything.
        /// </summary>
        private static string SimplifiedUpdatePayment(string paymExtId, int paymSubjTp, string paramsStr, string ackDescription)
        {
            using (SqlConnection conn = new SqlConnection(GlobalObjectsManager.PaymentDbConnectionString))
            {
                conn.Open();
                int paymId, paymState;
                if (!TryFindPayment(conn, paymExtId, out paymId, out paymState))
                    return BuildErrorResponse(paymExtId, "Payment not found.");

                AddPaymentParams(conn, paymId, paymSubjTp, paramsStr);
                return BuildAckResponse(paymId, paymState, paymExtId, ackDescription);
            }
        }

        private static string BuildAckResponse(int paymNumb, int paymState, string paymExtId, string description)
        {
            return "<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>OK</Result><PaymNumb>"
                + paymNumb + "</PaymNumb><PaymState>" + paymState + "</PaymState><PaymExtId>" + paymExtId
                + "</PaymExtId><Description>" + description + "</Description></Response>";
        }

        private static string BuildErrorResponse(string paymExtId, string description)
        {
            return "<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>Error</Result><PaymExtId>"
                + paymExtId + "</PaymExtId><Description>" + description + "</Description></Response>";
        }

        public Dispatcher()
        {
        }

        #region IHttpHandler Members


        /// <summary>
        /// Handles the HTTP request for the payment system.
        /// </summary>
        /// <param name="Context">Current HTTP context.</param>
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
                    Context.Response.Write("</PaymExtId><Description>Function is not supported.</Description></Response>");
                    return;
                }

                if (Function.ToLower() == "payment" && PaymExtId.Length > 20)
                    throw new Exception("PaymExtId invalid");



                // Simplified per product decision: the Signature/tosign crypto
                // check (MD5 hash vs certificate CN) was removed. Today only
                // the simplified Payment result is used - recording the
                // payment in the DB, without the two-phase flow involving
                // operators/jobs. Signature is now just passed through as-is
                // (or "EMPTY"), it no longer participates in any validation.
                if (string.IsNullOrEmpty(Signature))
                {
                    Signature = "EMPTY";
                }

                // Supports receiving client certificate data either directly
                // (legacy terminal -> IIS) or via the trusted nginx-mutual
                // proxy (X-Client-Cert-* headers) - see ClientCertHelper.
                string Subject = ClientCertHelper.GetDN(Context);
                int.TryParse(ClientCertHelper.GetO(Context), out org_id);
                string SerialNumber = ClientCertHelper.GetSerialNumber(Context);

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


                // Simplified per product decision: instead of calling the
                // shared SOAP service MessageProcessor.asmx (which internally
                // runs GetRek_20090918, quarantine checks, Job.GetAvailableJobs/
                // Job.Process - queueing the job for operator/job review, and
                // an SMS notification) - a self-contained simplified DB write
                // path is used. The shared MessageProcessor.asmx.cs itself is
                // left untouched (still used by OsmpDispatcher/PostProcessor).
                string result = null;
                if (Function.ToLower() == "payment")
                {
                    try
                    {
                        int paymAmount = 0;
                        int.TryParse(Amount, out paymAmount);
                        int totalSumInt = 0;
                        int.TryParse(TotalSum, out totalSumInt);
                        int serialNumberInt = 0;
                        int.TryParse(SerialNumber, out serialNumberInt);

                        int paymId = SimplifiedPutPayment(PaymExtId, PaymSubjTp, paymAmount, serialNumberInt,
                            totalSumInt, Signature, kopeks, payTypeId, Params);

                        result = BuildAckResponse(paymId, 2, PaymExtId, "Payment accepted.");
                    }
                    catch (Exception ex)
                    {
                        GlobalObjectsManager.Logger.Error("SimplifiedPutPayment", ex);
                        result = BuildErrorResponse(PaymExtId, "Error saving payment: " + ex.Message);
                    }
                }
                else if (Function.ToLower() == "check")
                {
                    try
                    {
                        int serialNumberIntForCheck = 0;
                        int.TryParse(SerialNumber, out serialNumberIntForCheck);
                        result = SimplifiedCheckPayment(PaymExtId, serialNumberIntForCheck, PaymSubjTp);
                    }
                    catch (Exception ex)
                    {
                        GlobalObjectsManager.Logger.Error("SimplifiedCheckPayment", ex);
                        result = BuildErrorResponse(PaymExtId, "Error checking payment: " + ex.Message);
                    }
                }
                else if (Function.ToLower() == "checkfull")
                {
                    try
                    {
                        int serialNumberInt = 0;
                        int.TryParse(SerialNumber, out serialNumberInt);
                        result = SimplifiedXmlPaymentInfo(PaymExtId, serialNumberInt);
                    }
                    catch (Exception ex)
                    {
                        GlobalObjectsManager.Logger.Error("SimplifiedXmlPaymentInfo", ex);
                        result = BuildErrorResponse(PaymExtId, "Error checking payment: " + ex.Message);
                    }
                }
                else if (Function.ToLower() == "update" || Function.ToLower() == "addparams")
                {
                    try
                    {
                        result = SimplifiedUpdatePayment(PaymExtId, PaymSubjTp, Params, "Payment updated.");
                    }
                    catch (Exception ex)
                    {
                        GlobalObjectsManager.Logger.Error("SimplifiedUpdatePayment", ex);
                        result = BuildErrorResponse(PaymExtId, "Error updating payment: " + ex.Message);
                    }
                }


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

            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("A system error occurred while processing the payment:", ex);
                if (utf8)
                    Context.Response.Write("<?xml version = \"1.0\" encoding = \"utf-8\"?><Response><Result>Error</Result><PaymExtId>");
                else
                    Context.Response.Write("<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>Error</Result><PaymExtId>");

                Context.Response.Write(PaymExtId);
                Context.Response.Write("</PaymExtId><Description>A system error occurred while processing the payment: ");
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
