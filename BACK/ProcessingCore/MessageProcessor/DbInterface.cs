using System;
using System.Data;
using System.Collections;
using Estylesoft.DALC;
using Estylesoft.Etran;
using System.Linq;

namespace MessageProcessor
{
    /// <summary>
    /// Класс, инкапсулирующий логику работы с БД службы.
    /// </summary>
    public sealed class DbInterface
    {
        private DbConnection _paymentDbConnection;

        /// <summary>
        /// Статический конструктор. Инициализирует провайдера и соединения в DALC.
        /// </summary>
        static DbInterface()
        {
            DbConnectionsManager.RegisterProvider(new SqlProvider());
            DbConnectionsManager.RegisterConnection("PaymentsDbConnection", EtranConfigurationManager.PaymentDbConnectionString, "sql");
        }

        /// <summary>
        /// Конструктор.
        /// </summary>
        public DbInterface()
        {
            _paymentDbConnection = DbConnectionsManager.Get("PaymentsDbConnection");
        }


        public int UpdatePayment(RequestMessage Request, string save_payment)
        {
            int ErrCode = 0;
            using (TransactionContext tc = DbManager.BeginTransaction())
            {
                try
                {
                    int requestID = 0;
                    using (DbDataReader messageParams = DbExecutor.ExecuteReader(
                              DbCommand.SP(
                              "AModule_UpdatePayment",
                              Request.PaymentID,
                              Request.Amount,
                              Request.PaymExtId,
                              Request.PaymSubjTp,
                              Request.Serial,
                              Request.TotalSum,
                              Request.Signature,
                              Request.UserId,
                              save_payment
                              )
                              , _paymentDbConnection))
                    {
                        if (messageParams.Read())
                        {
                            if (messageParams.GetString("result").ToLower() == "ok")
                            {
                                requestID = messageParams.GetInt32("Paym_id");
                                Request.Url = messageParams.GetString("Url");
                                Request.Rek = messageParams.GetString("Rek");
                                Request.Ps_Id = messageParams.GetInt32("Ps_Id");
                                ErrCode = messageParams.GetInt32("ErrCode");

                            }
                            else
                                if (messageParams.GetString("result").ToLower() == "error")
                            {
                                string mes = messageParams.GetString("descr");
                                throw new Exception(mes);
                            }
                            else
                                throw new Exception("Ошибка SQL.");

                        }
                    }


                    DbExecutor.ExecuteNonQuery(DbCommand.SP("AModule_DeletePaymentParam", requestID), _paymentDbConnection);

                    Hashtable requestParams = Request.GetParams();
                    foreach (DictionaryEntry p in requestParams)
                    {
                        if (DbExecutor.ExecuteNonQuery(DbCommand.SP("AModule_AddPaymentParam", requestID, Request.PaymSubjTp, p.Key, p.Value), _paymentDbConnection) == 0)
                        {
                            throw new EtranException(this.GetType().FullName, "AModule_AddPaymentParam", "Код параметра " + p.Key.ToString() + " не поддерживается кодом назначения " + Request.PaymSubjTp.ToString() + ".", null);
                        }
                    }

                    Request.PaymentID = (requestID > 0) ? requestID : 0;
                    tc.Commit();

                }
                catch (Exception)
                {
                    string param_in = "PutPaymentMessage() Function: " + Request.Function + " PaymExtId: " + Request.PaymExtId + " PaymSubjTp:" + Request.PaymSubjTp + " Amount: " + Request.Amount + " Params: " + Request.Params;
                    GlobalObjectsManager.Logger.Error(param_in);
                    tc.Rollback();
                    throw;
                }
            }
            return ErrCode;
        }

        public void SmsPayment(RequestMessage Request)
        {
            try
            {
                DbExecutor.ExecuteNonQuery(
                          DbCommand.SP(
                          "AModule_SmsPayment",
                          Request.Serial,
                          Request.PaymSubjTp,
                          Request.PaymExtId,
                          Request.Amount,
                          Request.TotalSum,
                          string.Join(",", Request.GetParams().Values.Cast<string>()
                                         .Select(x => x.ToString())
                                         .ToArray())
                          )
                          , _paymentDbConnection);
            }
            catch(Exception ex)
            {
                GlobalObjectsManager.Logger.Error(ex);
            }
        }
        public void AddTspAuto(int serial, int PaymSubjTp, string par_codes)
        {
            DbExecutor.ExecuteNonQuery(
                      DbCommand.SP(
                      "AModule_AddTspAuto",
                      serial,
                      PaymSubjTp,
                      par_codes
                      )
                      , _paymentDbConnection);
        }

        public int AddPaymentParam(RequestMessage Request)
        {
            int ErrCode = 0;
            int requestID = 0;
            using (DbDataReader messageParams = DbExecutor.ExecuteReader(
                      DbCommand.SP(
                      "AModule_GetPaymentID",
                      Request.PaymExtId
                      )
                      , _paymentDbConnection))
            {
                if (messageParams.Read())
                {
                    requestID = messageParams.GetInt32("Paym_id");
                }
            }

            if (requestID > 0)
            {
                using (TransactionContext tc = DbManager.BeginTransaction())
                {
                    try
                    {
                        Hashtable requestParams = Request.GetParams();
                        foreach (DictionaryEntry p in requestParams)
                        {
                            if (DbExecutor.ExecuteNonQuery(DbCommand.SP("AModule_AddPaymentParam", requestID, Request.PaymSubjTp, p.Key, p.Value), _paymentDbConnection) == 0)
                            {
                                throw new EtranException(this.GetType().FullName, "AModule_AddPaymentParam", "Код параметра " + p.Key.ToString() + " не поддерживается кодом назначения " + Request.PaymSubjTp.ToString() + ".", null);
                            }
                        }
                        Request.PaymentID = (requestID > 0) ? requestID : 0;
                        tc.Commit();
                    }
                    catch (Exception)
                    {
                        string param_in = "PutPaymentMessage() Function: " + Request.Function + " PaymExtId: " + Request.PaymExtId + " PaymSubjTp:" + Request.PaymSubjTp + " Amount: " + Request.Amount + " Params: " + Request.Params;
                        GlobalObjectsManager.Logger.Error(param_in);
                        tc.Rollback();
                        throw;
                    }
                }
            }
            return ErrCode;
        }

        static public Hashtable ParamsStringToHashtable(string Params)
        {
            Hashtable paramsTable = new Hashtable();
            string[] paramEntries = Params.Split(';');
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


        /// <summary>
        /// Кладет сообщение для обработки в БД. Если сообщение успешно помещено в БД,
        /// объекту Request присваивается свойство PaymentID.
        /// </summary>
        /// <param name="Request">Сообщение-запрос платежной системе.</param>
        public int PutPaymentMessage(RequestMessage Request)
        {
            bool ret_OK = false; // если больше суток то отвечаем OK что бы не стучался

            int ErrCode = 0;
            using (TransactionContext tc = DbManager.BeginTransaction())
            {
                try
                {
                    int requestID = 0;
                    using (DbDataReader messageParams = DbExecutor.ExecuteReader(
                              DbCommand.SP(
                              "AModule_PutPayment",
                              Request.Amount,
                              Request.PaymExtId,
                              Request.PaymSubjTp,
                              Request.Serial,
                              Request.TotalSum,
                              Request.Signature,
                              Request.PaymState,
                              Request.Kopeks,
                              Request.PayTypeId
                              )
                              , _paymentDbConnection))
                    {
                        if (messageParams.Read())
                        {
                            //if (messageParams[0].ToString() == "3")
                            //{
                            //    ErrCode = (int)messageParams["PaymState"];
                            //    requestID = messageParams.GetInt32("Paym_id");
                            //}
                            //else
                            try
                            {
                                if (messageParams.GetString("result").ToLower() == "ok")
                                {
                                    requestID = messageParams.GetInt32("Paym_id");
                                    Request.Url = messageParams.GetString("Url");
                                    Request.Rek = messageParams.GetString("Rek");
                                    Request.Ps_Id = messageParams.GetInt32("Ps_Id");
                                    //ErrCode = messageParams.GetInt32("ErrCode");
                                    Request.AltAmount = messageParams.GetInt64("AltAmount");
                                    Request.PaymState = messageParams.GetInt32("PaymState");

                                }
                                else
                                    if (messageParams.GetString("result").ToLower() == "error")
                                {
                                    string mes = messageParams.GetString("descr");
                                    throw new Exception(mes);
                                }
                                //    else
                                //      throw new Exception("Ошибка SQL.");
                            }
                            catch (Exception ex)
                            {
                                GlobalObjectsManager.Logger.Error(ex);
                                try
                                {
                                    ErrCode = (int)messageParams["PaymState"];
                                    requestID = messageParams.GetInt32("Paym_id");
                                }
                                catch { }

                            }

                        }
                    }

                    if (requestID > 0)
                    { }
                    else
                        throw new Exception("Ошибка SQL.");

                    GlobalObjectsManager.Logger.Info("AModule_PutPayment OK for PaymExtId: " + Request.PaymExtId);

                    Hashtable requestParams = ParamsStringToHashtable(Request.Params);
                    foreach (DictionaryEntry p in requestParams)
                    {

                        if (!System.Text.RegularExpressions.Regex.IsMatch(p.Key.ToString(), @"\d")) continue;

                        GlobalObjectsManager.Logger.Info(Request.PaymExtId + " Key: " + p.Key + " Value: " + p.Value);

                        if (DbExecutor.ExecuteNonQuery(DbCommand.SP("AModule_PutPaymentParam", requestID, Request.PaymSubjTp, p.Key, p.Value), _paymentDbConnection) == 0)
                        {
                            //try
                            //{
                            //    DateTime dt = DateTime.ParseExact(Request.PaymExtId.Substring(5, 6), "ddMMyy", null);
                            //    if (dt > DateTime.Now.AddDays(-3))
                            //    {
                            //        ret_OK = true;
                            //        GlobalObjectsManager.Logger.Info("QW2 ret_OK is true for PaymExtId: " + Request.PaymExtId + " dt: " + dt.ToString());
                            //    }

                            //}
                            //catch { }

                            //if (ret_OK && p.Key.ToString() == "1001")
                            //{
                            //    GlobalObjectsManager.Logger.Info("QW2 ignored 1001 for PaymExtId: " + Request.PaymExtId);
                            //}

                            //throw new EtranException(this.GetType().FullName, "AModule_PutPaymentParam", "Код параметра " + p.Key.ToString() + " не поддерживается кодом назначения " + Request.PaymSubjTp.ToString() + ".", null);

                            if (p.Key.ToString() != "1001")
                                throw new EtranException(this.GetType().FullName, "AModule_PutPaymentParam", "Код параметра " + p.Key.ToString() + " не поддерживается кодом назначения " + Request.PaymSubjTp.ToString() + ".", null);
                            else
                                GlobalObjectsManager.Logger.Info("ERROR1001 for PaymExtId: " + Request.PaymExtId);
                            //if (ret_OK)
                            //    GlobalObjectsManager.Logger.Info("QW2 ignored 1001 for PaymExtId: " + Request.PaymExtId);
                        }
                    }

                    Request.PaymentID = (requestID > 0) ? requestID : 0;
                    tc.Commit();

                }
                catch (Exception)
                {
                    string param_in = "APutPaymentMessage() Function: " + Request.Function + " PaymExtId: " + Request.PaymExtId + " PaymSubjTp:" + Request.PaymSubjTp + " Amount: " + Request.Amount + " Params: " + Request.Params;
                    GlobalObjectsManager.Logger.Error(param_in);
                    tc.Rollback();
                    //if (ret_OK)
                    //{
                    //    ErrCode = 1001;
                    //    GlobalObjectsManager.Logger.Info("QW2 refused OK for PaymExtId: " + Request.PaymExtId);
                    //}
                    //else
                    throw;
                }
            }
            return ErrCode;
        }


        public Hashtable GetMessageParams(object PaymentID)
        {
            Hashtable paramsTable = new Hashtable();
            using (DbDataReader messageParams = DbExecutor.ExecuteReader(DbCommand.SP("AModule_GetPaymentParams", PaymentID), _paymentDbConnection))
            {
                while (messageParams.Read())
                {
                    int key = messageParams.GetInt32("param_id");
                    string val = messageParams.GetString("param_value");

                    paramsTable.Add(key, val);
                }
                messageParams.Close();
            }
            return paramsTable;
        }

        public int GetOrgId(int serial, int paym_id)
        {
            int org_id = 0;
            using (DbDataReader messageParams = DbExecutor.ExecuteReader(DbCommand.SP("AModule_GetOrgId", serial, paym_id), _paymentDbConnection))
            {
                messageParams.Read();
                object o_org_id = messageParams["org_id"];
                org_id = (int)o_org_id;
                messageParams.Close();
            }
            return org_id;
        }


        ///// <summary>
        ///// Выуживает из БД Url и Rek по serial_number и TSP коду
        ///// </summary>
        ///// <param name="Request">Сообщение-запрос платежной системе.</param>
        //public string[] BCheckPayment(RequestMessage Request)
        //{
        //    using (DbDataReader messageParams = DbExecutor.ExecuteReader(
        //              DbCommand.SP(
        //              "BModule_CheckPayment",
        //              Request.Serial,
        //              Request.PaymSubjTp,
        //              Request.PaymExtId
        //              )
        //              , _paymentDbConnection))
        //    {
        //        messageParams.Read();
        //        Request.PaymentID = messageParams.GetInt32("PaymNumb");
        //        Request.PaymState = messageParams.GetInt32("PaymState");
        //        Request.Url = messageParams.GetString("Url");
        //        Request.Rek = messageParams.GetString("Rek");
        //        Request.Ps_Id = messageParams.GetInt32("Ps_Id");
        //        string att_descr = messageParams.GetString("Att_Descr");
        //        string AttDateTime = messageParams.GetString("AttDateTime");
        //        string OperatorAccepted = messageParams.GetString("OperatorAccepted");
        //        string[] ret = new string[3];
        //        ret[0] = att_descr;
        //        ret[1] = AttDateTime;
        //        ret[2] = OperatorAccepted;
        //        return ret;
        //    }
        //}


        public string[] CheckPayment(string paym_id, string serial_number, out RequestMessage Request)//, int serial_number)
        {
            Request = null;
            using (DbDataReader messageParams = DbExecutor.ExecuteReader(
                      DbCommand.SP(
                      "AModule_GetPayment", paym_id, serial_number
                      )
                      , _paymentDbConnection))
            {
                messageParams.Read();

                Hashtable htbl = new Hashtable();
                htbl.Add(1, 1);
                try
                {
                    Request = new RequestMessage(messageParams.GetInt32("paym_id"),
                        MessageFunctions.check, messageParams.GetString("PaymExtId"),
                        messageParams.GetInt32("PaymSubjTp"),
                        messageParams.GetInt64("paym_amount"),
                        htbl, 0, 0, "", "", "", 0, 0, 0, 0);
                    string att_descr = messageParams.GetString("Att_Descr");

                    string[] ret = new string[4];
                    ret[0] = att_descr;
                    return ret;
                }
                catch { }

            }
            return null;
        }


        /// <summary>
        /// Выуживает из БД Url и Rek по serial_number и TSP коду
        /// </summary>
        /// <param name="Request">Сообщение-запрос платежной системе.</param>
        public string[] CheckPayment(RequestMessage Request)
        {
            using (DbDataReader messageParams = DbExecutor.ExecuteReader(
                      DbCommand.SP(
                      "AModule_CheckPayment",
                      Request.Serial,
                      Request.PaymSubjTp,
                      Request.PaymExtId
                      )
                      , _paymentDbConnection))
            {
                messageParams.Read();
                Request.PaymentID = messageParams.GetInt32("PaymNumb");
                Request.PaymState = messageParams.GetInt32("PaymState");
                Request.Url = messageParams.GetString("Url");
                Request.Rek = messageParams.GetString("Rek");
                Request.Ps_Id = messageParams.GetInt32("Ps_Id");
                string att_descr = messageParams.GetString("Att_Descr");
                string AttDateTime = messageParams.GetString("AttDateTime");
                string OperatorAccepted = messageParams.GetString("OperatorAccepted");
                string Msg = messageParams.GetString("Msg");

                string[] ret = new string[4];
                ret[0] = att_descr;
                ret[1] = AttDateTime;
                ret[2] = OperatorAccepted;
                ret[3] = Msg;
                return ret;
            }
        }

        /// <summary>
        /// выдаем развернутую xml информацию по платежу
        /// </summary>
        public string XmlPaymentInfo(string paymextid, int serial_number)
        {
            string ret = null;
            using (DbDataReader messageParams = DbExecutor.ExecuteReader(
                      DbCommand.SP(
                      "AModule_XmlPaymentInfo",
                      paymextid,
                      serial_number
                      )
                      , _paymentDbConnection))
            {
                while (messageParams.Read())
                {
                    ret += messageParams.GetString(0);
                }
            }
            return ret;
        }


        /// <summary>
        /// Присваивает остановленное сообщение данному модулю и возвращает его экземпляр.
        /// </summary>
        /// <param name="PaymentID">Иденти фикатор платежа в БД Etran.</param>
        /// <returns>Экземпляр платежного сообщения.</returns>
        public RequestMessage PickStopedMessage(int PaymentID)
        {
            Hashtable paramsTable = new Hashtable();
            using (DbDataReader messageParams = DbExecutor.ExecuteReader(DbCommand.SP("AModule_GetPaymentParams", PaymentID), _paymentDbConnection))
            {
                while (messageParams.Read())
                {
                    int key = messageParams.GetInt32("param_id");
                    string val = messageParams.GetString("param_value");
                    paramsTable.Add(key, val);
                }

                messageParams.Close();
            }

            string PaymExtId = string.Empty;
            int PaymSubjTp = 0;
            long paym_amount = 0;
            long AltAmount = 0;
            int Serial = 0;
            int TotalSum = 0;
            string Url = string.Empty;
            string Rek = string.Empty;
            int Ps_Id = 0;


            using (DbDataReader msg = DbExecutor.ExecuteReader(DbCommand.SP("AModule_PickPayment", PaymentID), _paymentDbConnection))
            {
                if (msg.Read())
                {
                    PaymExtId = msg.GetString("PaymExtId");
                    PaymSubjTp = msg.GetInt32("PaymSubjTp");
                    paym_amount = msg.GetInt64("paym_amount");
                    Serial = msg.GetInt32("serial_number");
                    TotalSum = msg.GetInt32("totalsum");
                    Url = msg.GetString("Url");
                    Rek = msg.GetString("Rek");
                    Ps_Id = msg.GetInt32("Ps_Id");
                    AltAmount = msg.GetInt64("AltAmount");

                }

                msg.Close();
            }

            RequestMessage Request = new RequestMessage(PaymentID, MessageFunctions.payment, PaymExtId, PaymSubjTp, paym_amount, paramsTable, Serial, TotalSum, null, Url, Rek, Ps_Id, 0, 0, 0);
            Request.AltAmount = AltAmount;
            return Request;
        }

        public int ReportTry(object PaymentID, ResultStatus ResultStatus, string Description, int ps_id, DateTime OperatorAccepted, int os_id, int userid, long AltAmount, long AltNotice, long sms_number)
        {
            DbDataReader msg = DbExecutor.ExecuteReader(DbCommand.SP("AModule_ReportTryExt", PaymentID, (int)ResultStatus, Description, ps_id, OperatorAccepted, os_id, userid, AltAmount, AltNotice, sms_number), _paymentDbConnection);
            try
            {
                if (msg.Read())
                {
                    int paym_state = msg.GetInt32(0);
                    int ErrCount = msg.GetInt32(1);
                    int OkCount = msg.GetInt32(2);
                    //return (ErrCount == 0 && OkCount == 0) ? paym_state : 0;
                    return (OkCount == 0) ? paym_state : 0;

                }
            }
            catch { }
            { }
            return 0;
        }

        public int[] CustomSP(string name)
        {
            int[] paym_id = null;
            //paym_id = new int[1];
            //paym_id[0] = 52778632;
            DataSet msg = DbExecutor.ExecuteDataSet(DbCommand.SP(name), _paymentDbConnection);
            int count = msg.Tables[0].Rows.Count;
            paym_id = new int[count];
            for (int i = 0; i < count; i++)
                paym_id[i] = int.Parse(msg.Tables[0].Rows[i][0].ToString());

            return paym_id;
        }

        public string GetPaymentXmlForSave(int PaymentID)
        {
            string ret = string.Empty;
            using (DbDataReader msg = DbExecutor.ExecuteReader(DbCommand.SP("AModule_GetPaymentXmlForSave", PaymentID), _paymentDbConnection))
            {
                while (msg.Read())
                {
                    ret += msg.GetString(0);
                }
                msg.Close();
            }
            return ret;
        }

        public void ChangePaymentState(int paymentId, int newState)
        {
            DbExecutor.ExecuteNonQuery(DbCommand.SP("AModule_ChangePaymentState", paymentId, newState),
                _paymentDbConnection);
        }

    }
}
