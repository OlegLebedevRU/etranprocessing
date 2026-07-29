using System;
using System.Data;
using System.Web;
using System.Text;
using System.Collections;
using System.Collections.Specialized;
using System.Security.Cryptography.X509Certificates;
using System.Security.Cryptography;
using System.Xml;
using EtranLib.Data;

namespace EtranDispatcher
{
    /// <summary>
    /// ƒиспетчер сообщений Etran. ѕринимает и рассылает сообщени€ по
    /// рабочим серверам.
    /// </summary>
    public class Dispatcher : IHttpHandler
    {
        private const int RESPONSE_CODE_PAGE = 1251;
        private const string m_response = "<?xml version = \"1.0\" encoding = \"windows-1251\"?><response></response>";
        private const string m_response_error = "<?xml version = \"1.0\" encoding = \"windows-1251\"?><response><error>Ќевозможно обработать запрос.</error></response>";

        public Dispatcher()
        {
        }

        #region IHttpHandler Members



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

        public void ValidateResponse(Guid req_id, XmlDocument xml_in, XmlDocument xml_out)
        {
            XmlNodeList list_in = xml_out.SelectNodes("request/payment");
            XmlNodeList list_out = xml_out.SelectNodes("response/payment");
            if (list_in.Count != list_out.Count)
            {
                throw new Exception("Ќесовпадение тегов ответа с запросными.");
            }

        }

        static public string GetParams(int tsp_code, string req_params)
        {
            req_params = GlobalObjectsManager.ParamsTspCheck(tsp_code, req_params);
            string ret = string.Empty;
            string[] s_par = req_params.Split(';');
            DBManager db = new DBManager(EtranConfigurationManager.DBServiceConn);
            DataSet ds = (DataSet)db.Execute("MobilEasy_GetParamCode", CommandType.StoredProcedure, DBManager.DataReadType.DataSet, tsp_code);
            if (s_par.Length > ds.Tables[0].Rows.Count)
                throw new Exception(" оличество параметров платежа не верно.");

            for (int i = 0; i < s_par.Length; i++)
            {
                if (i > 0) ret += ";";
                ret += ds.Tables[0].Rows[i][0].ToString() + " " + s_par[i];
            }
            return ret;


        }


        static public void ParseDeviceStatus(string device_status, int TerminalID, int Serial)
        {
            string[] ds = device_status.Split(';');
            int validator_state = int.Parse(ds[0]);
            string printer_state = ds[1];
            while (printer_state.Length < 3)
                printer_state += "0";
            printer_state += " ";

            int banknote_counter = int.Parse(ds[2]);
            int cash_amount = int.Parse(ds[3]);
            int inkass_amount = int.Parse(ds[4]);
            //---------------- static
            int printer_check_counter = 0;
            int printer_fr = 0;
            int soft_version = 0;
            try
            {
                string sv = ds[37];
                if (sv.Length > 4)
                {
                    sv = sv.Substring(sv.Length - 4, 4);
                    soft_version = int.Parse(sv);
                }
            }
            catch { }
            int soft_state = 0;

            //return;

            DBManager db = new DBManager(EtranConfigurationManager.DBServiceConn);
            db.Execute("MobilEasy_SetKioskInfo", CommandType.StoredProcedure, DBManager.DataReadType.ExecuteNonQuery,
                TerminalID,
                Serial,
                validator_state,
                printer_state,
                banknote_counter,
                cash_amount,
                inkass_amount,
                printer_check_counter,
                printer_fr,
                soft_version,
                soft_state
                );



        }


        public void TEST(HttpContext Context)
        {
            long AmountAll = 0;
            string file;
            try
            {
                string[] files = System.IO.Directory.GetFiles(@"C:\OSMP_XML");
                for (int i = 0; i < files.Length; i++)//foreach (string file in files)
                {
                    file = files[i];
                    XmlDocument doc = new XmlDocument();
                    doc.Load(file);
                    //XmlNodeList list = doc.SelectNodes("request/auth/payment");
                    //string PaymExtId = list[i].SelectSingleNode("transaction-number").InnerText;
                    string TotalSum = doc.SelectSingleNode("request/auth/payment/from/amount").InnerText;
                    TotalSum = ((int)decimal.Parse(TotalSum.Replace('.', ','))).ToString();
                    AmountAll += long.Parse(TotalSum);
                    //string Amount = list[i].SelectSingleNode("to/amount").InnerText;
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine(ex.Message);
            }
        }


        static public string ReqHandler(Guid req_id, string req)
        {
            //string hhh = GlobalObjectsManager.ParamsTspCheck(1003, "1234567891239162376067");
            XmlDocument xml_response = new XmlDocument();
            xml_response.LoadXml(m_response);
            //return "";
            bool IsError = false;
            try
            {
                //XmlDocument doc = new XmlDocument();
                //doc.Load(@"11.xml");
                //req = doc.OuterXml;

                //req = GetData(Context);
                //GlobalObjectsManager.Logger.Info(req_id + " POST DATA: " + req);
                XmlDocument doc = new XmlDocument();
                doc.LoadXml(req);

                //long AmountAll = 0;
                //string file; 

                //string[] files = System.IO.Directory.GetFiles(@"C:\OSMP_XML");
                //for (int f = 0; f < files.Length; f++)
                //{
                //    file = files[f];
                //    XmlDocument doc = new XmlDocument();
                //    doc.Load(file);


                int req_type = int.Parse(doc.SelectSingleNode("request/request-type").InnerText);
                int TerminalID = int.Parse(doc.SelectSingleNode("request/terminal-id").InnerText);
                int SerialNumber = int.Parse(doc.SelectSingleNode("request/extra[@name='login']").InnerText);

                //string initialString = doc.SelectSingleNode("request/extra[@name='login']").InnerText;
                //System.Text.RegularExpressions.Regex nonNumericCharacters = new System.Text.RegularExpressions.Regex(@"[^0-9]");
                //string numericOnlyString = nonNumericCharacters.Replace(initialString, String.Empty);
                //int SerialNumber = int.Parse(numericOnlyString);
                //GlobalObjectsManager.Logger.Info("SerialNumber: " + SerialNumber);

                if (req_type == 3)
                {
                    string device_status = doc.SelectSingleNode("request/extra[@name='device-status']").InnerText;
                    ParseDeviceStatus(device_status, TerminalID, SerialNumber);
                }
                else
                    if (req_type == 10)
                    {
                        if (EtranConfigurationManager.PayLog != null && EtranConfigurationManager.PayLog.Length > 1)
                            doc.Save(EtranConfigurationManager.PayLog + TerminalID + "-" + req_id.ToString() + ".xml");

                        string Signature = string.Empty;   //doc.SelectSingleNode("request/extra[@name='password']").InnerText;
                        string count = doc.SelectSingleNode("request/auth/@count").Value;
                        int icount = int.Parse(count);
                        GlobalObjectsManager.Logger.Info(req_id + " COUNT: " + icount.ToString());
                        XmlNodeList list = doc.SelectNodes("request/auth/payment");
                        MessageProcessor processor = new MessageProcessor(EtranConfigurationManager.MessageProcessor);
                        //int AllAmount = 0;
                        for (int i = 0; i < list.Count; i++)
                        {
                            string Transaction = list[i].SelectSingleNode("transaction-number").InnerText;

                            string receipt_number = list[i].SelectSingleNode("receipt/receipt-number").InnerText;

                            string PaymExtId = list[i].SelectSingleNode("transaction-number").InnerText;
                            string TotalSum = list[i].SelectSingleNode("from/amount").InnerText;
                            string Amount = list[i].SelectSingleNode("to/amount").InnerText;
                            TotalSum = ((int)decimal.Parse(TotalSum.Replace('.', ','))).ToString();
                            Amount = ((int)(100 * decimal.Parse(Amount.Replace('.', ',')))).ToString();
                            if (int.Parse(TotalSum) >= int.Parse(Amount) / 100)
                            {
                                int PaymSubjTp = int.Parse(list[i].SelectSingleNode("to/service-id").InnerText);

                                GlobalObjectsManager.Logger.Info("DEBUG1 PaymSubjTp: " + PaymSubjTp);
                                string NewPaymSubjTp = GlobalObjectsManager.TspReplace[PaymSubjTp.ToString()];
                                GlobalObjectsManager.Logger.Info("DEBUG1 NewPaymSubjTp: " + NewPaymSubjTp);
                                if (NewPaymSubjTp != null)
                                    PaymSubjTp = int.Parse(NewPaymSubjTp);
                                GlobalObjectsManager.Logger.Info("DEBUG1 PaymSubjTp: " + PaymSubjTp);

                                string Params = list[i].SelectSingleNode("to/account-number").InnerText;
                                Params = GetParams(PaymSubjTp, Params);
                                string datetime = list[i].SelectSingleNode("receipt/datetime").InnerText;


                                // надо этот фикс сделать на терм ѕќ
                                //while (receipt_number.Length < 4)
                                //    receipt_number = ("0" + receipt_number);

                                //GlobalObjectsManager.Logger.Info("FIX TRY PaymExtId: " + PaymExtId + " terminal " + terminal + " receipt-number " + receipt_number);
                                //while (PaymExtId.Length > 16)
                                //    PaymExtId = PaymExtId.Remove(PaymExtId.Length - 1, 1);

                                //PaymExtId += receipt_number;
                                //GlobalObjectsManager.Logger.Info("FIX NEW PaymExtId: " + PaymExtId);



                                GlobalObjectsManager.Logger.Info(req_id + " TRY PAY...");
                                string result = processor.ProcessMessage("payment", PaymExtId, PaymSubjTp.ToString(), Amount, Params, SerialNumber.ToString(), TotalSum, Signature, "0");
                                //string result = "<Response><Result>OK</Result><PaymExtId>" + PaymExtId + "</PaymExtId><PaymState>93</PaymState><PaymNumb>0</PaymNumb><Description>успешно обработан.</Description></Response>";

                                XmlDocument xml_res = new XmlDocument();
                                xml_res.LoadXml(result);
                                string s_result = xml_res.SelectSingleNode("Response/Result").InnerText;
                                string state_pay = xml_res.SelectSingleNode("Response/PaymState").InnerText;
                                string sPaymNumb = xml_res.SelectSingleNode("Response/PaymNumb").InnerText;
                                long PaymNumb = long.Parse((sPaymNumb != null && sPaymNumb.Length > 0) ? sPaymNumb : "0");

                                XmlElement payment = xml_response.CreateElement("payment");
                                XmlAttribute status = xml_response.CreateAttribute("status");
                                XmlAttribute transaction = xml_response.CreateAttribute("transaction-number");
                                XmlAttribute result_code = xml_response.CreateAttribute("result-code");
                                XmlAttribute final_status = xml_response.CreateAttribute("final-status");
                                XmlAttribute fatal_error = xml_response.CreateAttribute("fatal-error");
                                transaction.Value = Transaction;
                                // по умолчанию result-code ставим ошибка не фатальна€ и финал статус = false то платеж будет стучатс€ € €щика пока не будет прин€т
                                status.Value = "0";
                                result_code.Value = "1";
                                final_status.Value = "false";
                                fatal_error.Value = "false";


                                if (s_result.ToUpper() == "OK" && PaymNumb > 0)
                                {
                                    status.Value = "25";
                                    result_code.Value = "0";
                                    final_status.Value = "false";
                                    fatal_error.Value = "false";
                                }
                                else
                                {
                                    if (state_pay == "93") // делаем проверку
                                    {
                                        string result_check = processor.ProcessMessage("check", PaymExtId, PaymSubjTp.ToString(), Amount, Params, SerialNumber.ToString(), TotalSum, "EMPTY", "0");
                                        //string result_check = "<Response><Result>OK</Result><PaymExtId>" + PaymExtId + "</PaymExtId><PaymState>2</PaymState><PaymNumb>222222222</PaymNumb><Description>успешно обработан.</Description></Response>";
                                        XmlDocument xml_res_check = new XmlDocument();
                                        xml_res_check.LoadXml(result_check);
                                        string s_result_check = xml_res_check.SelectSingleNode("Response/Result").InnerText;
                                        string state_check = xml_res_check.SelectSingleNode("Response/PaymState").InnerText;
                                        if (s_result_check.ToUpper() == "OK")
                                        {
                                            switch (state_check)
                                            {
                                                case "1":
                                                case "2":
                                                    {
                                                        status.Value = "60";
                                                        result_code.Value = "0";
                                                        final_status.Value = "true";
                                                        fatal_error.Value = "false";
                                                        break;
                                                    }
                                                default:
                                                    {
                                                        final_status.Value = "true";
                                                        fatal_error.Value = "true";
                                                        break;
                                                    }
                                            }
                                        }
                                        else // если по какой то причине проверка не удалась
                                        {
                                            final_status.Value = "false";
                                            fatal_error.Value = "false";
                                            GlobalObjectsManager.Logger.Error(req_id + " !!! ERROR.");
                                        }
                                    }
                                    else // платеж запостить не удалось, например не верный серийник
                                    {
                                        GlobalObjectsManager.Logger.Error(req_id + " !!! ERROR.");
                                        //XmlDocument xml_save = new XmlDocument();
                                        //xml_save.LoadXml(list[i].OuterXml);
                                        //xml_save.Save(EtranConfigurationManager.FailedPayLog + Transaction + ".xml");
                                        doc.Save(EtranConfigurationManager.FailedPayLog + Transaction + ".xml");
                                    }
                                }

                                payment.Attributes.Append(status);
                                payment.Attributes.Append(transaction);
                                payment.Attributes.Append(result_code);
                                payment.Attributes.Append(final_status);
                                payment.Attributes.Append(fatal_error);
                                xml_response.SelectSingleNode("response").AppendChild(payment);

                                //if(status.Value == "0" && final_status.Value == "false" && fatal_error.Value == "false")
                                //    GlobalObjectsManager.Logger.Error(req_id + " запостить платеж не удалось " + xml_response);
                                if (result_code.Value == "1")
                                    IsError = true;

                            }
                        }
                        processor.Dispose();
                    }

                // проверка ответа

                //GlobalObjectsManager.Logger.Info(req_id + "RESP: " + xml_response.OuterXml);
                //Context.Response.Write(xml_response.OuterXml);
                return xml_response.OuterXml;

                if (IsError && EtranConfigurationManager.PayLog != null && EtranConfigurationManager.PayLog.Length > 1)
                    doc.Save(EtranConfigurationManager.PayLog + "ERROR-" + TerminalID + "-" + req_id.ToString() + ".xml");

                //try { xml_response.Save(@"C:\LOG\" + req_id.ToString() + "-out.xml"); }
                //catch { }
                //}
            }
            catch (Exception ex)
            {
                //if (ex.Message.IndexOf("шестнадцатеричное значение 0x01") > -1 && req.IndexOf("<transaction-number>0564357365</transaction-number>") >-1)
                //{
                //    string ret = "<?xml version=\"1.0\" encoding=\"windows-1251\"?><response><payment status=\"60\" transaction-number=\"0564357365\" result-code=\"0\" final-status=\"true\" fatal-error=\"false\" /></response>";
                //    GlobalObjectsManager.Logger.Info("CATCH:" + ret);
                //    Context.Response.Write(ret);

                //}
                //else
                {
                    GlobalObjectsManager.Logger.Error(req_id + " —истемна€ ошибка: ", ex);
                    //GlobalObjectsManager.Logger.Error(req_id + " xml_response: " + xml_response.OuterXml);
                    //Context.Response.Write(m_response_error);
                }
            }
            finally
            {
            }
            return m_response_error;
        }

        /// <summary>
        /// ќбработчик HTTP-запроса пратежной систме.
        /// </summary>
        /// <param name="Context">“екущий HTTP-контекст.</param>
        public void ProcessRequest(HttpContext Context)
        {
            //TEST(Context);
            //return;

            Guid req_id = Guid.NewGuid();
            GlobalObjectsManager.Logger.Info(req_id + " " + Context.Request.Url);
            Context.Response.Charset = "windows-1251";
            Context.Response.ContentEncoding = Encoding.GetEncoding("windows-1251");
            Context.Response.ContentType = "text/xml";
            Context.Response.Cache.SetNoServerCaching();
            Context.Response.Cache.SetCacheability(HttpCacheability.NoCache);
            Context.Response.Cache.SetAllowResponseInBrowserHistory(false);

            string req = GetData(Context);

            GlobalObjectsManager.Logger.Info(req_id + " POST DATA: " + req);

            string ret = ReqHandler(req_id, req);

            GlobalObjectsManager.Logger.Info(req_id + " RESP: " + ret); ;

            Context.Response.Write(ret);

        }

        /// <summary>
        /// ќбработчик определ€етс€ как повторно используемый.
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
