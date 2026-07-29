using System;
using System.Globalization;
using System.Text;
using System.IO;
using System.Xml;
using System.Collections.Specialized;
using EtranLib.Collection;
using EtranLib.Net;
using EtranLib.Crypto;
using System.Collections;
using System.Web;
using EtranLib.Data;
using System.Data;
using System.Collections.Generic;
using System.Threading;
using log4net.Repository.Hierarchy;


public class bl
{
    private readonly string m_Function;
    private readonly string m_PaymExtId;
    private readonly string m_TerminalId;
    private readonly string m_PaymSubjTp;
    private readonly string m_Amount;
    private readonly int m_ConnectionTimeout;
    private readonly NameValueCollection m_Rek;
    private readonly NameValueCollection m_paym_params;
    private readonly DateTime m_dt;
    private string m_PaymentId = string.Empty;

    private string ret_xml = @"<Response><Result>%code%</Result><PaymExtId>%id%</PaymExtId><Description>%desc%</Description></Response>";

    private string m_err_for_dealer = string.Empty;

    private readonly string xml_req =
    "<?xml version=\"1.0\" encoding=\"windows-1251\"?>" +
"<request>" +
  "<auth login=\"" + "%login%" + "\" sign=\"" + "%sign%" + "\" signAlg=\"MD5\"/>" +
  "<client terminal=\"" + "%terminalid%" + "\" software=\"Dealer v0\"/>" +
"</request>";


    static readonly object _locker = new object();

    enFunctions m_Step = new enFunctions();
    enFunctions m_StepLast = new enFunctions();

    enum enFunctions { Start, getPaymentStatus, authorizePayment, confirmPayment };
    enum enResult { Start = 0, authorizeOK = 1, confirmOK = 2 };
    private enResult m_Result = new enResult();



    public bl(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek)
    {
        string param_in = "Function: " + Function + " PaymExtId: " + PaymExtId + " PaymSubjTp:" + PaymSubjTp + " Amount: " + Amount + " Params: " + Params + " ConnectionTimeout: " + ConnectionTimeout + " Rek:" + Rek;
        GlobalObjectsManager.Logger.Info("param_in: " + param_in);
        m_Function = Function.ToLower();
        m_PaymExtId = PaymExtId.Replace(" ", "");
        ret_xml = ret_xml.Replace("%id%", m_PaymExtId);
        m_PaymSubjTp = PaymSubjTp;
        double d = double.Parse(Amount) / 100;
        m_Amount = string.Format("{0:f}", d).Replace(',', '.');
        m_ConnectionTimeout = ConnectionTimeout * 1000;
        m_paym_params = Collection.GetNameValueCollection(Params, ";", " ");
        m_Rek = EtranLib.Collection.Collection.GetNameValueCollection(Rek, ";", "=");
        string rrr = m_Rek["rek"];
        GlobalObjectsManager.Logger.Info("REK: " + rrr);
        string[] Splitter = (m_Rek["rek"] != null) ? m_Rek["rek"].Split(':') : Rek.Split(';');

        m_TerminalId = Splitter[0];
        GlobalObjectsManager.Logger.Info("TERMINALID: " + m_TerminalId);
        GlobalObjectsManager.Logger.Info("LOGIN: " + Splitter[1]);
        GlobalObjectsManager.Logger.Info("PWD: " + Splitter[2]);

        xml_req = xml_req.Replace("%terminalid%", m_TerminalId);
        xml_req = xml_req.Replace("%login%", Splitter[1]);
        xml_req = xml_req.Replace("%sign%", EtranLib.Crypto.Crypto.MD5HashHex(Splitter[2]));
        if (m_Rek["pdt"] != null)
            m_dt = DateTime.Parse(m_Rek["pdt"]);
        else
            m_dt = DateTime.Now;

    }

    static XmlElement CreateElement(XmlDocument doc, string el_name, NameValueCollection attr)
    {
        XmlElement field = doc.CreateElement(el_name);
        if (attr != null)
            for (int i = 0; i < attr.Count; i++)
            {
                XmlAttribute xml_attr = doc.CreateAttribute(attr.Keys[i]);
                xml_attr.Value = attr[i];
                field.Attributes.Append(xml_attr);
            }
        return field;
    }

    private string CreateMessage()
    {
        string msg = string.Empty;

        XmlDocument xmldoc = new XmlDocument();
        xmldoc.LoadXml(xml_req);
        switch (m_Step)
        {
            case enFunctions.getPaymentStatus:
                {
                    XmlElement el_root = CreateElement(xmldoc, "providers", null);
                    XmlElement el_method = CreateElement(xmldoc, "getPaymentStatus", null);
                    el_root.AppendChild(el_method);
                    NameValueCollection nvc = new NameValueCollection();
                    nvc.Add("id", m_PaymentId);
                    XmlElement el_id = CreateElement(xmldoc, "payment", nvc);
                    el_method.AppendChild(el_id);
                    xmldoc.SelectSingleNode("request").AppendChild(el_root);
                    msg = xmldoc.OuterXml;
                    break;
                }
            case enFunctions.authorizePayment:
                {
                    XmlElement el_root = CreateElement(xmldoc, "providers", null);
                    XmlElement el_method = CreateElement(xmldoc, "authorizePayment", null);
                    el_root.AppendChild(el_method);

                    NameValueCollection nvc = new NameValueCollection();
                    nvc.Add("id", m_PaymentId);
                    XmlElement el_id = CreateElement(xmldoc, "payment", nvc);
                    el_method.AppendChild(el_id);
                    nvc.Clear();
                    nvc.Add("amount", m_Amount);
                    nvc.Add("currency", "643");
                    XmlElement el_from = CreateElement(xmldoc, "from", nvc);
                    el_id.AppendChild(el_from);
                    nvc.Clear();
                    nvc.Add("service", GlobalObjectsManager.GetPrvId(m_PaymSubjTp, m_paym_params));
                    try
                    {
                        string account = m_paym_params["1"] ?? m_paym_params[0];
                        try
                        {
                            string paramIn = account;
                            if (m_PaymSubjTp == "572")
                            {
                                if (account[0].ToString(CultureInfo.InvariantCulture).ToUpper() != "R")
                                    account = "R" + account;
                            }else
                            if (m_PaymSubjTp == "609")
                            {
                                account = "R" + m_paym_params["1"] +";"+ m_paym_params["2"];
                            }

                            if(paramIn != account)
                                GlobalObjectsManager.Logger.Info(m_PaymExtId + " PARAM FIX FROM " + paramIn + " TO " + account);
                        }
                        catch (Exception ex)
                        {
                            GlobalObjectsManager.Logger.Error(ex);
                        }

                        nvc.Add("account", account);
                    }
                    catch (Exception ex)
                    {
                        m_err_for_dealer = "Ошибка. Некорректный параметр платежа;fatal";
                        GlobalObjectsManager.Logger.Error(ex);
                        throw new Exception(m_err_for_dealer);
                    }

                    nvc.Add("amount", m_Amount);
                    nvc.Add("currency", "643");
                    XmlElement el_to = CreateElement(xmldoc, "to", nvc);
                    el_id.AppendChild(el_to);
                    nvc.Clear();
                    nvc.Add("id", m_PaymentId);
                    nvc.Add("date", m_dt.ToString("yyyy-MM-ddTHH:mm:ss"));
                    XmlElement el_receipt = CreateElement(xmldoc, "receipt", nvc);
                    el_id.AppendChild(el_receipt);
                    xmldoc.SelectSingleNode("request").AppendChild(el_root);
                    msg = xmldoc.OuterXml;
                    break;
                }
            case enFunctions.confirmPayment:
                {
                    XmlElement el_root = CreateElement(xmldoc, "providers", null);
                    XmlElement el_method = CreateElement(xmldoc, "confirmPayment", null);
                    el_root.AppendChild(el_method);
                    NameValueCollection nvc = new NameValueCollection();
                    nvc.Add("id", m_PaymentId);
                    XmlElement el_id = CreateElement(xmldoc, "payment", nvc);
                    el_method.AppendChild(el_id);
                    xmldoc.SelectSingleNode("request").AppendChild(el_root);
                    msg = xmldoc.OuterXml;
                    break;
                }

        }
        return msg;
    }

    public long GetAmount()
    {
        long amount = 0;
        try
        {
            XmlDocument xmldoc = new XmlDocument();
            xmldoc.LoadXml(xml_req);
            XmlElement el_root = CreateElement(xmldoc, "agents", null);
            XmlElement el_method = CreateElement(xmldoc, "getBalance", null);
            el_root.AppendChild(el_method);
            xmldoc.SelectSingleNode("request").AppendChild(el_root);
            string msg = xmldoc.OuterXml;

            GlobalObjectsManager.Logger.Info("BALANCE SEND MSG: " + msg);
            string ret = Net.WebRequest(EtranConfigurationManager.PaymentUrl, m_ConnectionTimeout, msg, null);
            GlobalObjectsManager.Logger.Info("BALANCE RECV MSG: " + ret);

            XmlDocument doc = new XmlDocument();
            doc.LoadXml(ret);
            string desc = string.Empty;
            XmlNode node_op = doc.SelectSingleNode("*/*/getBalance");
            string balance = node_op["balance"].InnerText;
            decimal db = decimal.Parse(balance.Replace('.', ','));
            amount = (long)(db * 100);

            string overdraft = node_op["overdraft"].InnerText;
            if (overdraft != null)
            {
                decimal de_overdraft = decimal.Parse(overdraft.Replace('.', ','));
                long l_overdraft = (long)(de_overdraft * 100);
                amount += l_overdraft * -1;
            }
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error("GetAmount", ex);
        }
        return amount;
    }

    public string CancelPayment(string id)
    {
        string result = "OK";
        try
        {
            XmlDocument xmldoc = new XmlDocument();
            xmldoc.LoadXml(xml_req);
            XmlElement el_root = CreateElement(xmldoc, "providers", null);
            XmlElement el_method = CreateElement(xmldoc, "cancelPayment", null);
            NameValueCollection nvc = new NameValueCollection();
            nvc.Add("id", id);
            XmlElement el_payment = CreateElement(xmldoc, "payment", nvc);
            el_method.AppendChild(el_payment);
            el_root.AppendChild(el_method);
            xmldoc.SelectSingleNode("request").AppendChild(el_root);
            string msg = xmldoc.OuterXml;
            xmldoc.Save(GlobalObjectsManager.curr_path + "Send-CancelPaymentID-" + id + ".xml");

            GlobalObjectsManager.Logger.Info("CancelPayment SEND MSG: " + msg);
            string ret = Net.WebRequest(EtranConfigurationManager.PaymentUrl, m_ConnectionTimeout, msg, null);
            GlobalObjectsManager.Logger.Info("CancelPayment RECV MSG: " + ret);

            XmlDocument doc = new XmlDocument();
            doc.LoadXml(ret);
            xmldoc.Save(GlobalObjectsManager.curr_path + "Recv-CancelPaymentID-" + id + ".xml");
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error("CancelPayment", ex);
        }
        return result;
    }



    private void Response(string msg)
    {
        XmlDocument doc = new XmlDocument();
        doc.LoadXml(msg);
        string desc = string.Empty;
        int result = -1;
        int status = -1;
        string date = null;
        string uid = null;

        int result_op = int.Parse(doc["response"].Attributes["result"].Value);
        if (result_op == 0)
        {
            XmlNode node_op = doc.SelectSingleNode("*/*/" + m_Step.ToString());
            result_op = int.Parse(node_op.Attributes["result"].Value);

            if (result_op == 0)
            {
                XmlNode node = node_op.SelectSingleNode("//*[@id='" + m_PaymentId + "']");
                result = int.Parse(node.Attributes["result"].Value);
                status = int.Parse(node.Attributes["status"].Value);
                date = (node.Attributes["date"] == null) ? null : node.Attributes["date"].Value;
                uid = (node.Attributes["uid"] == null) ? null : node.Attributes["uid"].Value;

                switch (m_Step)
                {
                    case enFunctions.getPaymentStatus:
                        {
                            if (m_Result == 0 && result == 210)
                            {
                                m_Step = enFunctions.authorizePayment;
                                GlobalObjectsManager.Logger.Info(m_PaymExtId + " getPaymentStatus OK");
                            }
                            else
                                if (status == 3)
                                {
                                    if (m_Result == enResult.authorizeOK)
                                    {
                                        GlobalObjectsManager.Logger.Info(m_PaymExtId + " !!! not confirmed try confirmPayment");
                                        m_Step = enFunctions.confirmPayment;
                                    }
                                }
                            break;
                        }
                    case enFunctions.authorizePayment:
                        {
                            if (status == 3 && result == 0)
                            {
                                m_Result = enResult.authorizeOK;
                                m_Step = enFunctions.confirmPayment;
                                GlobalObjectsManager.Logger.Info(m_PaymExtId + " authorizePayment OK");
                                DB_FixId();
                                GlobalObjectsManager.Logger.Info(m_PaymExtId + " authorizePayment DB OK");
                            }
                            break;
                        }

                    case enFunctions.confirmPayment:
                        {
                            if (status == 1)
                            {
                                GlobalObjectsManager.Logger.Info(m_PaymExtId + " confirmPayment OK");
                                m_Result = enResult.confirmOK;
                                m_Step = enFunctions.getPaymentStatus;
                                DB_FixId();
                            }
                            break;
                        }
                }
            }
        }

        // processing done
        if (m_Step == m_StepLast)
        {
            GlobalObjectsManager.Logger.Info(m_PaymExtId + " processing done status: " + status);
            //20110722 если платеж авторизован успешно но на данный момент есть какие то проблемы то пишем ПРОВОДИТСЯ что бы избежать дублирования
            if (status == 1)
            {
                ret_xml = ret_xml.Replace("%code%", "ERROR");
                ret_xml = ret_xml.Replace("%desc%", "Проводится");
            }
            else
                if (status == 2)
                {
                    ret_xml = ret_xml.Replace("%code%", "OK");
                    ret_xml = ret_xml.Replace("%desc%", "Проведен;" + date + ";" + uid);
                }
                else
                {
                    int err_code = (result_op != 0) ? result_op : result;
                    if (err_code > 0)
                    {
                        ret_xml = ret_xml.Replace("%code%", "ERROR");
                        string descr = GlobalObjectsManager.GetError(err_code);
                        ret_xml = ret_xml.Replace("%desc%", descr);
                    }
                    else
                    {
                        throw new Exception("Необработанная ситуация.");
                    }
                }
        }
    }


    public string SendAndSave(string node_name, string req_name, NameValueCollection attr, string save_name)
    {
        string ret = "OK";
        try
        {
            XmlDocument doc = new XmlDocument();
            XmlDocument xmldoc = new XmlDocument();
            xmldoc.LoadXml(xml_req);
            XmlElement el_root = CreateElement(xmldoc, node_name, null);
            XmlElement el_method = CreateElement(xmldoc, req_name, attr);
            el_root.AppendChild(el_method);
            xmldoc.SelectSingleNode("request").AppendChild(el_root);
            string msg = xmldoc.OuterXml;

            GlobalObjectsManager.Logger.Info("SendAndSave snd " + msg);
            ret = Net.WebRequest(EtranConfigurationManager.PaymentUrl, m_ConnectionTimeout, msg, null);
            GlobalObjectsManager.Logger.Info("SendAndSave ret " + ret);
            doc.LoadXml(ret);
            string path = GlobalObjectsManager.curr_path + save_name + ".xml";
            doc.Save(path);
            ret = "OK";
        }
        catch (Exception ex)
        {
            ret = ex.Message;
        }
        return ret;
    }

    public static string XmlToXml(string msg)
    {
        string ret;
        try
        {
            GlobalObjectsManager.Logger.Info("XmlToXml snd " + msg);
            ret = Net.WebRequest(EtranConfigurationManager.PaymentUrl, 60000, msg, null);
            GlobalObjectsManager.Logger.Info("XmlToXml ret " + ret);
            XmlDocument doc = new XmlDocument();
            doc.LoadXml(ret);
            string fileName = DateTime.Now.ToString("yyyyMMddHHmmssffff");
            string path = GlobalObjectsManager.curr_path + fileName + ".xml";
            doc.Save(path);
            ret = path;
        }
        catch (Exception ex)
        {
            ret = ex.Message;
        }
        return ret;
    }


    private string Request()
    {
        XmlDocument doc = new XmlDocument();
        string msg = CreateMessage();
        GlobalObjectsManager.Logger.Info("SEND-" + m_Step.ToString() + "-" + m_dt.ToString("yyyyMMddHHmmss") + "-" + m_PaymExtId + ": " + msg);
        string ret = Net.WebRequest(EtranConfigurationManager.PaymentUrl, m_ConnectionTimeout, msg, null);
        GlobalObjectsManager.Logger.Info("RECV-" + m_Step.ToString() + "-" + m_dt.ToString("yyyyMMddHHmmss") + "-" + m_PaymExtId + ": " + ret);
        return ret;
    }



    private void DB_GetPaymentID()
    {
        string id = string.Empty;
        Monitor.Enter(_locker);
        try
        {
            DBManager db = new DBManager(EtranConfigurationManager.DbConnectionString);
            NameValueCollection ds = (NameValueCollection)db.Execute("MasterPort_GetPaymentID", CommandType.StoredProcedure, DBManager.DataReadType.NameValueCollection, null, m_TerminalId, m_PaymExtId);
            m_PaymentId = ds["PaymentId"];
            m_Result = ((enResult)int.Parse(ds["Result"]));
            GlobalObjectsManager.Logger.Info(m_PaymExtId + " DB_GetPaymentID " + m_PaymentId + " " + m_Result);

            if (m_PaymentId != null && m_PaymentId.Length > 0)
            {
                //OK
            }
            else
            {
                throw new Exception("Не удалось установить m_TerminalID");
            }
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error(m_PaymExtId + " DB_GetPaymentID", ex);
            throw new Exception("DB ERROR.");
        }
        finally { Monitor.Exit(_locker); }
    }

    private void DB_FixId()
    {
        Monitor.Enter(_locker);
        try
        {
            DBManager db = new DBManager(EtranConfigurationManager.DbConnectionString);
            GlobalObjectsManager.Logger.Info(m_PaymExtId + " DB_FixId " + m_TerminalId + " " + m_PaymExtId + " " + m_PaymentId + " " + (int)m_Result);
            db.Execute("MasterPort_FixId", CommandType.StoredProcedure, DBManager.DataReadType.ExecuteNonQuery, null, m_TerminalId, m_PaymExtId, m_PaymentId, (int)m_Result);
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error(m_PaymExtId + " DB_FixId", ex);
            throw new Exception("DB ERROR.");
        }
        finally { Monitor.Exit(_locker); }
    }

    public string DoRequest()
    {
        try
        {
            switch (m_Function)
            {
                case "check":
                    ret_xml = ret_xml.Replace("%code%", "OK");
                    ret_xml = ret_xml.Replace("%desc%", "OK");
                    break;
                case "payment":
                    DB_GetPaymentID();
                    m_Step = enFunctions.getPaymentStatus;
                    break;
            }

            while (m_Step != m_StepLast)
            {

                if (m_StepLast != enFunctions.Start && m_Step == enFunctions.getPaymentStatus)
                    System.Threading.Thread.Sleep(EtranConfigurationManager.StatusWait);

                m_StepLast = m_Step;
                Response(Request());

            }

            if (ret_xml.IndexOf("%code%") > -1)
            {
                throw new Exception("Обработчик не найден.");
            }
        }
        catch (Exception e)
        {
            GlobalObjectsManager.Logger.Error(m_PaymExtId, e);
            ret_xml = ret_xml.Replace("%code%", "ERROR");
            if (m_err_for_dealer != null && m_err_for_dealer.Length > 0)
                ret_xml = ret_xml.Replace("%desc%", m_err_for_dealer);
            else
                ret_xml = ret_xml.Replace("%desc%", "server OSMP unavailable.");
        }
        finally
        {
            GlobalObjectsManager.Logger.Info("RET: " + ret_xml);
        }
        return ret_xml;
    }
}
