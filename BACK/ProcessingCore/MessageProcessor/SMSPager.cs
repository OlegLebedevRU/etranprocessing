using System;
using System.Xml;
using System.Data;
using System.Configuration;
using System.Web;
using System.Web.Security;
using System.Web.UI;
using System.Web.UI.WebControls;
using System.Web.UI.WebControls.WebParts;
using System.Web.UI.HtmlControls;
using Estylesoft.Etran;
using System.Collections;
using EtranLib.Net;

namespace MessageProcessor
{
    public class SMSPager
    {

        static string CheckAccount(string _number)
        {
            string number = _number;
            try
            {
                if (number.Length == 11)
                {
                    if (number[0] != '7')
                        number = "7" + number.Remove(0, 1);
                    return number;
                }
                else
                    if (number.Length == 10)
                    {
                        number = "7" + number;
                        return number;
                    }
            }
            catch
            {
            }
            return null;

        }

        static private string Send(long phone, string msg, int id) 
        {
            string ret = string.Empty;
            string sms_url = EtranConfigurationManager.EtranConfig + "?function=sms&phone=" + phone + "&msg=" + msg;
            GlobalObjectsManager.Logger.Info("sms_url: " + sms_url);
            ret = (new System.Net.WebClient()).DownloadString(sms_url);
            GlobalObjectsManager.Logger.Info("sms_ret: " + ret);
            return ret;
        }

        static public void Send(long number, int paym_state, RequestMessage RequestMessage, string Description)
        {
            GlobalObjectsManager.Logger.Info("TRY Send: " + number + " state: " + paym_state);
            string msg = string.Empty;
            try
            {
                //switch (paym_state)
                //{
                //    case 2:
                //        msg = "проведен";
                //        break;
                //    case 6:
                //        msg = "не проведен (ошибка номером)";
                //        break;
                //    case 3:
                //        msg = "задержка в проведении";
                //        break;
                //    default:
                //        //msg = "UNKNOWN";
                //        GlobalObjectsManager.Logger.Info("SMSPager UNKNOWN state for number:" + number + " state: " + paym_state);
                //        break;
                //}

                msg = Environment.NewLine + "Plat " + RequestMessage.PaymExtId;
                if (RequestMessage.PaymSubjTp == 609)
                {
                    try
                    {
                        string[] rrr = Description.Split(';');
                        msg += ", Session " + rrr[0];
                    }
                    catch (Exception ex) { GlobalObjectsManager.Logger.Error("SMSPager", ex); }
                }

                if (msg.Length > 0)
                    Send(number, msg, RequestMessage.PaymentID);
            }
            catch (Exception ex) { GlobalObjectsManager.Logger.Error("SMSPager", ex); }
        }

        static public long IsSend(RequestMessage msg)
        {
            try
            {
                if (msg.Ps_Id == 10)
                {
                    Hashtable requestParams = msg.GetParams();
                    string param_code = EtranConfigurationManager.SMS_param_code;
                    bool eee = requestParams.ContainsKey(param_code);
                    string number = (eee) ? requestParams[param_code].ToString() : null;
                    if (number != null && number.Length>0)
                    {
                        string num = CheckAccount(number);
                        long lnum = long.Parse(num);
                        return lnum;
                    }
                }
            }
            catch (Exception ex) { GlobalObjectsManager.Logger.Error("SMSPager, paym_id: " + msg.PaymentID, ex); }
            return 0;
        }


        //static public long AltNotice(RequestMessage msg, string status, string Description)
        //{
        //    try
        //    {
        //        if (msg.PaymState == 0 && status.ToLower() != "ok")  // если толкнули через AssignMessage т.е. ошибка уже не первый раз то ничего не пишем.
        //            return 0;
        //        string number = IsSend(msg);
        //        if (number != null && number.Length > 0)
        //        {
        //            return EtranConfigurationManager.SMS_cost;
        //        }
        //        else
        //            return 0;
        //    }

        //    catch (Exception ex) { GlobalObjectsManager.Logger.Error("SMSPager", ex); }
        //    return 0;

        //}
        
        //static public long Send(RequestMessage msg, string status)
        //{
        //    try
        //    {
        //        if (msg.PaymState == 0 && status.ToLower() != "ok")  // если толкнули через AssignMessage т.е. ошибка уже не первый раз то ничего не пишем.
        //            return 0;
        //        string number = IsSend(msg);
        //        if (number != null && number.Length > 0)
        //        {
        //            Send(number, status, Description);
        //            return EtranConfigurationManager.SMS_cost;
        //        }
        //        else
        //            return 0;
        //    }


    }
}
