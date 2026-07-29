using System;
using System.Data;
using System.Web;
using System.Collections;
using System.Web.Services;
using System.Web.Services.Protocols;
using System.ComponentModel;
using System.Threading;
using Estylesoft.DALC;
using Estylesoft.Etran;
using System.Xml;
using System.Linq;

namespace MessageProcessor
{
    /// <summary>
    /// Summary description for Service1
    /// </summary>
    [WebService(Namespace = "http://tempuri.org/")]
    [WebServiceBinding(ConformsTo = WsiProfiles.BasicProfile1_1)]
    [ToolboxItem(false)]
    public class MessageProcessor : System.Web.Services.WebService
    {
        //[WebMethod]
        //public int MyTest()
        //{
        //    Hashtable requestParams = DbInterface.ParamsStringToHashtable("1 11;2 22;3 ТРИШИНА НАТАЛЬЯ АЛЕКСЕЕВНА;4 РБЮВ085/17-с;5 ТРИШИНА НАТАЛЬЯ АЛЕКСЕЕВНА");
        //    Hashtable requestParams1 = DbInterface.ParamsStringToHashtable("1 11;2 ;;3 ТРИШИНА НАТАЛЬЯ АЛЕКСЕЕВНА;4 РБЮВ085/17-с;5 ТРИШИНА НАТАЛЬЯ АЛЕКСЕЕВНА");
        //    foreach (DictionaryEntry p in requestParams1)
        //    {
        //        if (System.Text.RegularExpressions.Regex.IsMatch(p.Key.ToString(), @"\d"))
        //        {
        //            int a = 0;
        //        }
        //        else
        //        {
        //            int y = 0;
        //        }

        //    }

        //        return 0;
        //}

        [WebMethod]
        public short PaySystemsTimeOut()
        {
            return EtranConfigurationManager.PaySystemsTimeOut;
        }


        [WebMethod]
        public string ModuleName()
        {
            return EtranConfigurationManager.ModuleName;
        }

        [WebMethod]
        public long GetAvailableJobs()
        {
            return Job.GetAvailableJobs;
        }


        static string AddAttDateTime(string resp, string time, string oa)
        {
            string AttDateTime = "<AttDateTime>%AttDateTime%</AttDateTime><OperatorAccepted>%oa%</OperatorAccepted>";
            string exc;
            try
            {
                AttDateTime = AttDateTime.Replace("%AttDateTime%", (oa.Length > 1) ? oa : time);
                AttDateTime = AttDateTime.Replace("%oa%", oa);
                int indx = resp.IndexOf("</Response>");
                resp = resp.Insert(indx, AttDateTime);

            }
            catch (Exception e)
            {
                exc = e.Message;
            }
            return resp;
        }

        [WebMethod]
        public string XmlPaymentInfo(string paymextid, int serial_number)
        {
            string ret = "<?xml version = \"1.0\" encoding = \"windows-1251\"?><root>";
            //string ret = "<?xml version = \"1.0\" encoding = \"utf-8\"?><root>";
            try
            {
                DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                ret += dbInterface.XmlPaymentInfo(paymextid, serial_number);
            }
            catch (Exception ex)
            {
                ret += ex.Message;
            }
            ret += "</root>";
            return ret;
        }

        //<Response><Result>OK</Result><PaymNumb>92419940</PaymNumb><PaymState></PaymState><PaymExtId>0424_260711_16462071</PaymExtId><Description>2011-07-26 16:56:14 - Проведен;2011-07-26T16:49:10+04:00;10172557317001</Description></Response>
        [WebMethod]
        public string CheckPayment(string PaymNumb, string serial_number)
        {
            try
            {
                DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                RequestMessage Request = null;
                string[] att_descr = dbInterface.CheckPayment(PaymNumb, serial_number, out Request);
                if (att_descr != null)
                    return DefaultMessages.GetDefaultOKResponse(Request, att_descr[0]).ToString();
                else
                    return "<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>ERROR</Result><Description>Платеж не найден</Description></Response>";
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("CheckPayment : " + PaymNumb, ex);
            }
            return "<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response>Непредвиденная ошибка</Response>";
        }

        [WebMethod]
        public string ProcessMessage(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, string Serial,
            string TotalSum, string Signature, string userid, string PaymState, string kopeks, string payTypeId)
        //public string ProcessMessage()
        {
            //2019 - 11 - 19 12:57:47,147 - SerialNumber: 8529, Function: payment, PaymExtId: 0235_191119_12551551, PaymSubjTp: 1000301, Amount: 77900, Params:
            //1 180103:Профессиональная гигиеническая подготовка должностных лиц, работников организаций, индивидуальных предпринимателей(1 человек):60589 | 180101:Аттестация по профессиональной гигиенической подготовке должностных лиц, работников организаций, индивидуальных предпринимателей(  кроме иностранных граждан)(1 человек) :17311; 2 779, TotalSum: 779,0000, kopeks 0, Subject: C = ru, S = msk, L = 956, O = 457, OU = 235, CN = CA0CA88D92
            //2011-05-26 11:23:24,993 - SerialNumber: 6959, Function: payment, PaymExtId: 0010_250511_15555337, PaymSubjTp:374, Amount: 20000, Params: 9 9280695259;10 90401000000;8 0;11 1515900318;1 32110807020011000110;7 20000;3 ЛОТИЕВА;4 ГАЛИНА;5 АНДРЕЕВНА;6 Г.МОЗДРК\УЛ.ГУРЖИБЕКОВА\Д.65В;2 -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=, TotalSum: 210, Subject: C=ru, S=msk, L=943, O=215, OU=10, CN=7D303D7247EA6BFD
            //09071395E0669459, E=1.terminal@forpay.ru, Signature: ERROR
            //string Function = "check";
            //string PaymExtId = "0005_141119_16351345";
            //string PaymSubjTp = "1000301";
            //string Amount = "30000";
            //string Params = @"1 180103:Профессиональная гигиеническая подготовка должностных лиц, работников организаций, индивидуальных предпринимателей(1 человек):60589 | 180101:Аттестация по профессиональной гигиенической подготовке должностных лиц, работников организаций, индивидуальных предпринимателей(  кроме иностранных граждан)(1 человек) :17311; 2 779";
            ////string Params = @"1 Профессиональная гигиеническая подготовка должностных лиц, работников организаций, индивидуальных предпринимателей (1 человек):60589;180101:Аттестация;2 779";
            //string Serial = "8437";
            //string TotalSum = "300";
            //string Signature = "EMPTY";
            //string userid = "4";
            //string PaymState = "1";
            //string kopeks = "0";
            //string payTypeId = "0";
            return _ProcessMessage(Function, PaymExtId, PaymSubjTp, Amount, Params, Serial, TotalSum, Signature, userid, PaymState, kopeks, payTypeId);
        }


        static System.Collections.Generic.HashSet<string> tspAuto = new System.Collections.Generic.HashSet<string>();
        public string _ProcessMessage(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params,
            string Serial, string TotalSum, string Signature, string userid, string PaymState, string kopeks, string payTypeId)
        {

            const int PAYS_QUEUE_MIN = 5;
            GlobalObjectsManager.Logger.Info("Вызван метод ProcessMessage tspAuto.Count: " + tspAuto.Count);
            if (tspAuto.Count > 0)
            {
                foreach(var ta in tspAuto)
                    GlobalObjectsManager.Logger.Info("tspAuto: " + ta);
            }
            int i_userid = 0;
            try
            {
                i_userid = int.Parse(userid);
            }
            catch
            { }

            if (string.IsNullOrEmpty(kopeks))
                kopeks = "0";

            string param_in = "Function: " + Function + " PaymExtId: " + PaymExtId + " PaymSubjTp:" + PaymSubjTp + " Amount: " + Amount
                + " Params: " + Params + " Serial: " + Serial + " TotalSum: " + TotalSum + " Signature: " + Signature + " userid: "
                + userid + " PaymState: " + PaymState + " kopeks: " + kopeks;

            GlobalObjectsManager.Logger.Info(param_in);
            RequestMessage message = new RequestMessage(0, Function, PaymExtId, PaymSubjTp, Amount, Params, int.Parse(Serial), int.Parse(TotalSum), Signature, null, null, 0, i_userid, int.Parse(kopeks), int.Parse(payTypeId));

            int iPaymState = (int)PaymStates.start;
            try
            {
                if (PaymState != null && PaymState.Length > 0) iPaymState = int.Parse(PaymState);
                message.PaymState = iPaymState;
            }
            catch { }

            try
            {
                // check org_id for TimeZone 
                DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                int org_id = dbInterface.GetOrgId(int.Parse(Serial), 0);
                if (org_id == 0)
                    throw new Exception("Не найден оргид.");
                bool blocked = TimeZone.IsBlocked(org_id);
                if (blocked)
                    return DefaultMessages.GetDefaultErrorResponse(message, "Технический перерыв.").ToString();
                else
                {
                    Hashtable requestParams = message.GetParams();

                    if (tspAuto.Contains(message.PaymExtId))
                    {
                        tspAuto.Remove(message.PaymExtId);
                        dbInterface.AddTspAuto(message.Serial, message.PaymSubjTp,
                             string.Join(",", requestParams.Keys.Cast<object>()
                                         .Select(x => x.ToString())
                                         .ToArray())
                            );
                    }

                    if (message.Function == MessageFunctions.check)
                    {
                        //DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                        string[] att_descr = dbInterface.CheckPayment(message);
                        if (message.PaymentID > 0)
                        {
                            //return DefaultMessages.GetDefaultOKResponse(message, att_descr[0]).ToString();
                            string ResponseXml = DefaultMessages.GetDefaultOKResponse(message, att_descr[0]).ToString();
                            string ResponseXmlAndDT = AddAttDateTime(ResponseXml, att_descr[1], att_descr[2]);
                            GlobalObjectsManager.Logger.Info("ResponseXmlAndDT: " + ResponseXmlAndDT);

                            return ResponseXmlAndDT;
                        }
                        else
                        {
                            if (message.Url != null && message.Url.Length > 0)
                                //return "<?xml version = \"1.0\" encoding = \"utf-8\"?>" + EtranConfigurationManager.DoRequest(message);
                                return "<?xml version = \"1.0\" encoding = \"windows-1251\"?>" + EtranConfigurationManager.DoRequest(message);
                            else
                            {
                                string ret = DefaultMessages.GetDefaultErrorResponse(message, "Системная ошибка.").ToString();
                                if (att_descr[3] != null && att_descr[3].Length > 0)
                                {
                                    XmlDocument doc = new XmlDocument();
                                    doc.LoadXml(ret);
                                    XmlElement nEl = doc.CreateElement("commands");
                                    XmlElement nEl2 = doc.CreateElement("command");
                                    nEl2.InnerText = att_descr[3];
                                    nEl.AppendChild(nEl2);
                                    doc.SelectSingleNode("Response").AppendChild(nEl);
                                    ret = doc.OuterXml;
                                }
                                return ret;
                            }
                        }
                    }
                    else if (message.Function == MessageFunctions.payment)
                    {
                        try
                        {
                            //DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                            if (message.PaymState == 1 && Job.GetAvailableJobs < PAYS_QUEUE_MIN)
                            {
                                message.PaymState = (int)PaymStates.stop;
                                GlobalObjectsManager.Logger.Error("Нет свободных обработчиков для PaymExtId: " + message.PaymExtId);
                            }
                            int ErrCode = dbInterface.PutPaymentMessage(message);
                            if(ErrCode==0) dbInterface.SmsPayment(message);
                            if (ErrCode == 0 && Job.GetAvailableJobs >= PAYS_QUEUE_MIN && message.PaymState == 1)
                                Job.Process(message);
                            return DefaultMessages.GetDefaultOKResponse(message).ToString();
                        }
                        catch (Exception ex)
                        {
                            GlobalObjectsManager.Logger.Error("При обращении к службе возникло исключение.", ex);
                            message.PaymState = (int)PaymStates.stop;
                            if (ex.Message.IndexOf("Cannot insert duplicate key") > -1)
                            {
                                message.PaymState = 93;
                                return DefaultMessages.GetDefaultOKResponse(message, "Платеж с таким PaymExtId уже был, для получения статуса платежа пожайлуста используйте функцию check.").ToString();
                            }
                            throw;
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                if (message !=null && !string.IsNullOrEmpty(message.PaymExtId) && EtranConfigurationManager.IsTspAutoAdd(message.PaymSubjTp))
                {
                    tspAuto.Add(message.PaymExtId);
                }
                GlobalObjectsManager.Logger.Error(param_in, ex);
                return DefaultMessages.GetDefaultErrorResponse(message, "Системная ошибка.").ToString();
            }
            finally
            {
            }
            return "??!!";
        }


        //[WebMethod]
        //public string AAAA()
        //{

        //    TimeSpan dd = DateTime.UtcNow.TimeOfDay;
        //    int utc_add = 20;
        //    TimeSpan dt = DateTime.UtcNow.AddHours(utc_add).TimeOfDay;
        //    //TimeZone.Init();

        //    //DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
        //    //int org_id = dbInterface.GetOrgId(4798,0);//(5081, 0);
        //    bool f = TimeZone.IsBlocked(33);
        //    //return f.ToString();
        //    return "";

        //}

        [WebMethod]
        public string UpdatePayment(string PaymentID, string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, string Serial, string TotalSum, string Signature, string userid)
        //public string UpdatePayment()
        {

            //string PaymentID = "0";
            //string Function = "addparams";
            //string PaymExtId = "0121_070719_14015367";
            //string PaymSubjTp = "1000373";
            //string Amount = "0";
            //string Params = "301 09203580867;302 патик;303 9203580867;304 0121_300719204828_00213";
            //string Serial = "8393";
            //string TotalSum = "0";
            //string Signature = "empty";
            //string userid = "0";

            GlobalObjectsManager.Logger.Info("Вызван метод UpdatePayment");
            string param_in = " paym_id: " + PaymentID + "Function: " + Function + " PaymExtId: " + PaymExtId + " PaymSubjTp:" + PaymSubjTp + " Amount: " + Amount + " Params: " + Params + " userid: " + userid;
            GlobalObjectsManager.Logger.Info(param_in);
            RequestMessage message = new RequestMessage(int.Parse(PaymentID), Function.ToLower() == "addparams" ? "update" : Function, PaymExtId, PaymSubjTp, Amount, Params, int.Parse(Serial), int.Parse(TotalSum), Signature, null, null, 0, int.Parse(userid), 0, 0);
            string ret = DefaultMessages.GetDefaultErrorResponse(message, "Системная ошибка.").ToString();

            try
            {
                DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();

                if (Function.ToLower() == "addparams")
                {
                    dbInterface.AddPaymentParam(message);
                    return DefaultMessages.GetDefaultOKResponse(message).ToString();
                }
                else
                {
                    // check org_id for TimeZone 
                    int org_id = dbInterface.GetOrgId(0, message.PaymentID);
                    if (org_id == 0)
                        throw new Exception("Не найден оргид.");
                    bool blocked = TimeZone.IsBlocked(org_id);
                    if (blocked)
                        return DefaultMessages.GetDefaultErrorResponse(message, "Технический перерыв.").ToString();
                    else
                    {
                        //Hashtable requestParams = message.GetParams();
                        //DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                        string save_payment = dbInterface.GetPaymentXmlForSave(message.PaymentID);
                        XmlDocument doc = new XmlDocument();
                        doc.LoadXml("<root>" + save_payment + "</root>");
                        string paym_id = doc.LastChild["Payment"].Attributes["paym_id"].Value;
                        if (PaymentID != paym_id)
                            throw new Exception("Ошибка GetPaymentXmlForSave.");
                        int ErrCode = dbInterface.UpdatePayment(message, save_payment);
                        if (ErrCode == 0 && Job.GetAvailableJobs > 0)
                        {
                            message.PaymState = (int)PaymStates.start;
                            Job.Process(message);
                            //AssignMessage(message.PaymentID);
                        }
                        else
                            message.PaymState = (int)PaymStates.stop;
                    }
                    return DefaultMessages.GetDefaultOKResponse(message).ToString();
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error(param_in, ex);
                message.PaymState = (int)PaymStates.stop;
                if (ex.Message.IndexOf("Cannot insert duplicate key") > -1)
                {
                    message.PaymState = 93;
                    return DefaultMessages.GetDefaultOKResponse(message, "Платеж с таким PaymExtId уже был, для получения статуса платежа пожайлуста используйте функцию check.").ToString();
                }
            }
            return ret;
        }

        [WebMethod]
        public bool ChangePaymentState(string paymentId, string newState)
        {
            string param_in = " paym_id: " + paymentId + " newState: " + newState;
            try
            {
                GlobalObjectsManager.Logger.Info("Вызван метод ChangePaymentState");
                GlobalObjectsManager.Logger.Info(param_in);
                DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                dbInterface.ChangePaymentState(int.Parse(paymentId), int.Parse(newState));
                return true;
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error(param_in, ex);
            }
            return false;
        }

        /// <summary>
        /// Передает остановленое сообщение из БД на обработку данному модулю.
        /// </summary>
        /// <param name="PaymentID">Идентификатор платежа.</param>
        [WebMethod]
        public void AssignMessage(int PaymentID)
        {
            try
            {
                GlobalObjectsManager.Logger.Info("Вызван метод AssignMessage c ID = " + PaymentID.ToString());
                if (Job.GetAvailableJobs < 1) throw new Exception("Нет свободных обработчиков.");

                // check org_id for TimeZone 
                DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                int org_id = dbInterface.GetOrgId(0, PaymentID);
                if (org_id == 0)
                    throw new Exception("Не найден оргид.");
                bool blocked = TimeZone.IsBlocked(org_id);
                if (blocked)
                    throw new Exception("Технический перерыв.");
                else
                {

                    //DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                    RequestMessage message = dbInterface.PickStopedMessage(PaymentID);
                    Job.Process(message);

                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("Возникло исключение при вызове AssignMessage", ex);
                throw;
            }
        }


        [WebMethod]
        public void RunPaymentFromCustomSP(string name_sp)
        {
            try
            {
                //GlobalObjectsManager.Logger.Info("Вызван метод AssignMessage c ID = " + PaymentID.ToString());
                if (Job.GetAvailableJobs < 1) throw new Exception("Нет свободных обработчиков.");

                DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                int[] paym_id = dbInterface.CustomSP(name_sp);
                for (int i = 0; i < paym_id.Length; i++)
                {
                    try
                    {
                        RequestMessage message = dbInterface.PickStopedMessage(paym_id[i]);
                        Job.Process(message);
                        GlobalObjectsManager.Logger.Info("RunPaymentFromCustomSP START OK  paym_id: " + paym_id[i]);
                    }
                    catch (Exception ex)
                    {
                        GlobalObjectsManager.Logger.Error("RunPaymentFromCustomSP PAYMENT paym_id: " + paym_id[i], ex);
                    }
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("RunPaymentFromCustomSP", ex);
                throw;
            }
        }

        [WebMethod]
        public void AssignMessageParams(int paym_id, string PaymExtId, int PaymSubjTp, long paym_amount, int serial_number, int totalsum, string url, string rek, int ps_id, long AltAmount)
        {
            try
            {
                GlobalObjectsManager.Logger.Info("Вызван метод AssignMessageParams c ID = " + paym_id.ToString());
                if (Job.GetAvailableJobs < 1) throw new Exception("Нет свободных обработчиков.");
                // check org_id for TimeZone 
                DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                int org_id = dbInterface.GetOrgId(serial_number, 0);
                if (org_id == 0)
                    throw new Exception("Не найден оргид.");
                bool blocked = TimeZone.IsBlocked(org_id);
                if (blocked)
                    throw new Exception("Технический перерыв.");
                else
                {

                    //DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                    RequestMessage message = new RequestMessage(paym_id, MessageFunctions.payment, PaymExtId, PaymSubjTp, paym_amount, dbInterface.GetMessageParams(paym_id), serial_number, totalsum, null, url, rek, ps_id, 0, 0, 0);
                    message.AltAmount = AltAmount;
                    Job.Process(message);
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("Возникло исключение при вызове AssignMessageParams", ex);
                throw;
            }
        }
    }
}
