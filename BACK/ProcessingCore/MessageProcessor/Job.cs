using System;
using System.Xml;
using System.Threading;
using Estylesoft.Etran;
using System.Collections;

namespace MessageProcessor
{
    public class Job
    {

        static public void Process(RequestMessage _Message)
        {

            //_Message.PaymentID
            //if (_Message.Amount > EtranConfigurationManager.PaymentLimit * 100) return;
            ThreadStart starter = delegate { RequestProcess(_Message); };
            new Thread(starter).Start();
        }

        static public long GetAvailableJobs
        {
            get
            {
                long limit = EtranConfigurationManager.JobsQueueCapacity;
                return (limit - Interlocked.Read(ref GlobalObjectsManager.JobsCounter));
            }
        }


        static private void RequestProcess(object param)
        {

            RequestMessage Message = (RequestMessage)param;
            Interlocked.Increment(ref GlobalObjectsManager.JobsCounter);

            try
            {
                object obj = GlobalObjectsManager.PaymentList[Message.PaymentID];
                if (obj != null)
                    throw new Exception("PaymentList PaymentID: " + Message.PaymentID + " уже находится в обработке.");

                GlobalObjectsManager.PaymentList.Add(Message.PaymentID, 0);

                GlobalObjectsManager.Logger.Info("S:PaymentID " + Message.PaymentID);
                ResultStatus ResultStatus = ResultStatus.Error;
                string Description = string.Empty;
                string Result = string.Empty;
                DateTime OperatorAccepted = new DateTime();
                int os_id = 0;
                long notice_alt = 0;
                long sms_number = SMSPager.IsSend(Message);
                if (sms_number > 0)
                {
                    if (Message.Amount > EtranConfigurationManager.SMS_cost)
                        Message.Amount -= EtranConfigurationManager.SMS_cost;
                    if (Message.AltAmount > EtranConfigurationManager.SMS_cost)
                        Message.AltAmount -= EtranConfigurationManager.SMS_cost;
                }

                if (Message.Amount <= EtranConfigurationManager.CheckLimit(Message.PaymSubjTp))
                {
                    try
                    {
                        // test 5 kiosk EASYPAY
                        if(EtranConfigurationManager.SERIAL_TEST != null && EtranConfigurationManager.SERIAL_TEST.Length > 0)
                            if (Message.Serial.ToString() == EtranConfigurationManager.SERIAL_TEST)
                                Message.Url = "http://bl.corepay.local/GateMoney/";

                        //
                        string response = EtranConfigurationManager.DoRequest(Message);
                        XmlDocument doc = new XmlDocument();
                        doc.LoadXml(response);
                        Description = doc.SelectSingleNode("Response/Description").InnerText;
                        Result = doc.SelectSingleNode("Response/Result").InnerText.ToUpper();

                        try
                        {
                            XmlNode node = doc.SelectSingleNode("Response/os_id");
                            if (node != null)
                                os_id = int.Parse(node.InnerText);
                        }
                        catch
                        {
                        }

                        if (Result == "OK")
                        {
                            ResultStatus = ResultStatus.PayOK;
                            try
                            {

                                XmlNode node = doc.SelectSingleNode("Response/Status");
                                if (node != null)
                                    ResultStatus = (ResultStatus)int.Parse(node.InnerText);

                                node = doc.SelectSingleNode("Response/OperatorAccepted");
                                if (node != null)
                                    try
                                    {
                                        OperatorAccepted = DateTime.Parse(node.InnerText);
                                    }
                                    catch { }

                            }
                            catch (Exception e)
                            {
                                GlobalObjectsManager.Logger.Error("ExtError: ", e);
                            }
                        }
                    }
                    catch (Exception ex)
                    {
                        GlobalObjectsManager.Logger.Error("При обращении к модулю [" + Message.Ps_Id + "] возникло исключение", ex);
                        Description = ex.Message;
                    }

                    notice_alt = (sms_number > 0) ? EtranConfigurationManager.SMS_cost : 0;
                    if (notice_alt > 0)
                    {
                        GlobalObjectsManager.Logger.Info("notice_alt:" + notice_alt + " ID " + Message.PaymentID + " sms_number " + sms_number);
                    }
                }
                else
                {
                    Description = EtranConfigurationManager.PaymentLimitErrorMessage;
                }

                DbInterface dbInterface = GlobalObjectsManager.GetDbInterface();
                int paym_state = dbInterface.ReportTry(Message.PaymentID, ResultStatus, Description, Message.Ps_Id, OperatorAccepted, os_id, Message.UserId, Message.AltAmount, notice_alt, sms_number);

                if (sms_number > 0 && paym_state == 2)
                    SMSPager.Send(sms_number, paym_state, Message, Description);


            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("При обращении к службе возникло исключение.", ex);
            }
            finally
            {
                try
                {
                    GlobalObjectsManager.PaymentList.Remove(Message.PaymentID);
                }
                catch(Exception e)
                {
                    GlobalObjectsManager.Logger.Error("PaymentList.Remove.", e);
                }

                Interlocked.Decrement(ref GlobalObjectsManager.JobsCounter);
                if (Message != null && Message.PaymentID > 0)
                    GlobalObjectsManager.Logger.Info("D:PaymentID " + Message.PaymentID);
            }
            return;
        }
    }
}
