using System;
using System.IO;
using System.Xml;
using System.Collections.Specialized;
using EtranLib.Collection;
using EtranLib.Net;
using EtranLib.Xml;
using System.Collections;
using System.Web;
using EtranLib.Data;
using System.Data;
using System.Collections.Generic;
using System.Threading;




public class bl
{
    private DateTime m_dt;
    private string m_Function;
    private string m_PaymExtId;
    private string m_transaction_number;
    private string m_PaymSubjTp;
    private string m_Amount;

    private string m_TerminalID = string.Empty;
    private string m_PWD = string.Empty;

    private int m_ConnectionTimeout;
    private NameValueCollection m_Rek;
    private NameValueCollection m_paym_params;
    private NameValueCollection m_Accounts = new NameValueCollection();

    static readonly object _locker = new object();

    enOperation m_Step = new enOperation();
    enOperation m_StepLast = new enOperation();

    enum enOperation { Start, Reestr, Providers, ResultCodes, Balance, CheckStatus, Payment };

    private static Hashtable htbl_Balance = new Hashtable();

    private string ret_xml = @"<Response><Result>%code%</Result><PaymExtId>%id%</PaymExtId><Description>%desc%</Description></Response>";

    private static readonly string xml_req =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>" +
    "<request>" +
    "<protocol-version>4.00</protocol-version>" +
    "<request-type>%type%</request-type>" +
    "<extra name=\"token\">%pwd%</extra>" +
    "<extra name=\"client-software\">xml-PLATERRA v1</extra>" +
    "<terminal-id>%terminalid%</terminal-id>" +
    "</request>";

    public bl(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek)
    {
        string param_in = "Function: " + Function + " PaymExtId: " + PaymExtId + " PaymSubjTp:" + PaymSubjTp + " Amount: " + Amount + " Params: " + Params + " ConnectionTimeout: " + ConnectionTimeout + " Rek:" + Rek;
        GlobalObjectsManager.Logger.Info("param_in: " + param_in);
        m_Function = Function.ToLower();
        m_PaymSubjTp = PaymSubjTp;
        m_PaymExtId = PaymExtId.Replace(" ", "");
        ret_xml = ret_xml.Replace("%id%", m_PaymExtId);

        m_transaction_number = ((uint)(m_PaymExtId.GetHashCode())).ToString();

        if (m_Function == "payment")
        {
            Amount = GlobalObjectsManager.TryTspAmountFix(m_PaymSubjTp, Amount);
        }

        double d = double.Parse(Amount) / 100;
        m_Amount = string.Format("{0:f}", d).Replace(',', '.');
        GlobalObjectsManager.Logger.Info("PaymExtId" + PaymExtId + " m_Amount " + m_Amount);

        m_ConnectionTimeout = ConnectionTimeout * 1000;
        m_paym_params = Collection.GetNameValueCollection(Params, ";", " ");

        m_Rek = EtranLib.Collection.Collection.GetNameValueCollection(Rek, ";", "=");

        string rrr = m_Rek["rek"];
        GlobalObjectsManager.Logger.Info("REK: " + rrr);
        string[] Splitter = (m_Rek["rek"] != null) ? m_Rek["rek"].Split(':') : Rek.Split(';');
        GlobalObjectsManager.Logger.Info("REK Splitter LEN: " + Splitter.Length);
        for (int i = 0; i < Splitter.Length; i += 2)
        {
            string TerminalID = Splitter[i];
            string PWD = Splitter[i + 1];
            GlobalObjectsManager.Logger.Info("ACCOUNTS N: " + (i / 2) + " TerminalID= " + TerminalID + " PWD= " + PWD);
            m_Accounts.Add(TerminalID, PWD);
        }

        if (m_Rek["pdt"] != null)
            m_dt = DateTime.Parse(m_Rek["pdt"]);
        else
            m_dt = DateTime.Now;

    }

    private string DB_GetTerminalID(string PaymExtId)
    {
        DBManager db = new DBManager(EtranConfigurationManager.PaymentDbConnectionString);
        NameValueCollection ds = (NameValueCollection)db.Execute("Qiwi_GetTerminalID", CommandType.StoredProcedure, DBManager.DataReadType.NameValueCollection, null, PaymExtId);
        return ds["TerminalID"];
    }

    private void DB_PutTerminalID(string TerminalID, string PaymExtID)
    {
        DBManager db = new DBManager(EtranConfigurationManager.PaymentDbConnectionString);
        db.Execute("Qiwi_PutPayment", CommandType.StoredProcedure, DBManager.DataReadType.ExecuteNonQuery, null, TerminalID, PaymExtID);
    }

    private static void Balance_Update(string tid, long balance)
    {
        Monitor.Enter(_locker);
        try
        {
            if (htbl_Balance[tid] != null)
            {
                htbl_Balance[tid] = balance;
            }
            else
            {
                htbl_Balance.Add(tid, balance);
            }
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error("Balance_Update", ex);
        }
        finally { Monitor.Exit(_locker); }
    }

    private static string Balance_GetTerminalID(long paym_amount, NameValueCollection Accounts)
    {
        Monitor.Enter(_locker);
        try
        {
            for (int i = 0; i < Accounts.Count; i++)
            {
                string TID = Accounts.Keys[i];

                if (htbl_Balance[TID] != null)
                {
                    string balance = htbl_Balance[TID].ToString();
                    decimal db = decimal.Parse(balance.Replace('.', ','));
                    long lb = (long)(db * 100);
                    if (paym_amount < lb)
                        return TID;
                    else
                        Accounts.Remove(TID);
                }
            }
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error("Balance_GetTerminalID ", ex);
        }
        finally { Monitor.Exit(_locker); }
        return (Accounts.Count > 0) ? Accounts.Keys[0] : null;
    }

    private string GetTerminalID()
    {

        GlobalObjectsManager.Logger.Error("GetTerminalID TRY FOR: " + m_PaymExtId);

        string TerminalID = DB_GetTerminalID(m_PaymExtId);
        
        GlobalObjectsManager.Logger.Error("GetTerminalID GOT FOR: " + m_PaymExtId + " " +TerminalID);

        long num = long.Parse(TerminalID);
        if (num > 0)
        {
            return TerminalID;
        }
        else
        {
            GlobalObjectsManager.Logger.Error("GetTerminalID FROM DB NOT FOUND FOR " + m_PaymExtId + " " + TerminalID);
            decimal db = decimal.Parse(m_Amount.Replace('.', ','));
            long lb = (long)(db * 100);
            return Balance_GetTerminalID(lb, m_Accounts);
        }
        return null;
    }


    private string CreateMessage()
    {
        string msg = xml_req;

        msg = msg.Replace("%terminalid%", m_TerminalID);
        msg = msg.Replace("%pwd%", m_PWD);

        XmlDocument xmldoc = new XmlDocument();
        xmldoc.LoadXml(msg);
        switch (m_Step)
        {
            case enOperation.Providers:
                {
                    xmldoc.LastChild["request-type"].InnerText = "17";
                    NameValueCollection nvc = new NameValueCollection();

                    nvc.Add("name", "Full");
                    XmlElement el_full = XmlClass.CreateElement(xmldoc, "extra", nvc);
                    el_full.InnerText = "1";

                    nvc.Clear();
                    nvc.Add("name", "with_folder");
                    XmlElement el_with_folder = XmlClass.CreateElement(xmldoc, "extra", nvc);
                    el_with_folder.InnerText = "1";

                    XmlElement el_from = XmlClass.CreateElement(xmldoc, "from", null);
                    XmlElement el_to = XmlClass.CreateElement(xmldoc, "to", null);

                    XmlElement el_service_id = XmlClass.CreateElement(xmldoc, "service-id", null);
                    el_service_id.InnerText = "0";
                    el_from.AppendChild(el_service_id);
                    el_to.AppendChild(el_service_id);

                    XmlElement el_account_number = XmlClass.CreateElement(xmldoc, "account-number", null);
                    el_account_number.InnerText = "0";
                    el_from.AppendChild(el_account_number);
                    el_to.AppendChild(el_account_number);

                    xmldoc["request"].AppendChild(el_full);
                    xmldoc["request"].AppendChild(el_with_folder);
                    xmldoc["request"].AppendChild(el_from);
                    xmldoc["request"].AppendChild(el_to);

                    break;
                }
            case enOperation.Reestr:
                {

                    xmldoc.LastChild["request-type"].InnerText = "8";
                    NameValueCollection nvc = new NameValueCollection();

                    nvc.Add("name", "date-from");
                    XmlElement el_from = XmlClass.CreateElement(xmldoc, "extra", nvc);
                    DateTime dt_from = DateTime.Parse(m_paym_params[0]);
                    string str_from = dt_from.ToString("dd.MM.yyyy");
                    el_from.InnerText = str_from;

                    nvc.Clear();
                    nvc.Add("name", "date-to");
                    XmlElement el_to = XmlClass.CreateElement(xmldoc, "extra", nvc);
                    DateTime dt_to = DateTime.Parse(m_paym_params[1]);
                    string str_to = dt_to.ToString("dd.MM.yyyy");
                    el_to.InnerText = str_to;

                    xmldoc["request"].AppendChild(el_from);
                    xmldoc["request"].AppendChild(el_to);
                    break;
                }
            case enOperation.Balance:
                {
                    xmldoc.LastChild["request-type"].InnerText = "3";
                    break;
                }
            case enOperation.ResultCodes:
                {
                    xmldoc.LastChild["request-type"].InnerText = "6";
                    break;
                }
            case enOperation.CheckStatus:
                {

                    xmldoc.LastChild["request-type"].InnerText = "10";
                    NameValueCollection nvc = new NameValueCollection();
                    nvc.Add("count", "1");

                    XmlElement el_method = XmlClass.CreateElement(xmldoc, "status", nvc);
                    XmlElement el_payment = XmlClass.CreateElement(xmldoc, "payment", null);
                    XmlElement el_id = XmlClass.CreateElement(xmldoc, "transaction-number", null);
                    el_id.InnerText = m_transaction_number;
                    el_payment.AppendChild(el_id);
                    el_method.AppendChild(el_payment);

                    xmldoc.SelectSingleNode("request").AppendChild(el_method);
                    break;
                }
            case enOperation.Payment:
                {
                    xmldoc.LastChild["request-type"].InnerText = "10";
                    NameValueCollection nvc = new NameValueCollection();
                    nvc.Add("count", "1");
                    nvc.Add("to-amount", m_Amount);
                    XmlElement el_root = XmlClass.CreateElement(xmldoc, "auth", nvc);
                    XmlElement el_method = XmlClass.CreateElement(xmldoc, "payment", null);
                    el_root.AppendChild(el_method);
                    nvc.Clear();

                    XmlElement el_id = XmlClass.CreateElement(xmldoc, "transaction-number", null);
                    el_id.InnerText = m_transaction_number;
                    el_method.AppendChild(el_id);

                    XmlElement el_from = XmlClass.CreateElement(xmldoc, "from", null);
                    XmlElement el_amount_from = XmlClass.CreateElement(xmldoc, "amount", null);
                    el_amount_from.InnerText = m_Amount;
                    el_from.AppendChild(el_amount_from);
                    el_method.AppendChild(el_from);

                    XmlElement el_to = XmlClass.CreateElement(xmldoc, "to", null);

                    XmlElement el_service_id = XmlClass.CreateElement(xmldoc, "service-id", null);
                    el_service_id.InnerText = EtranConfigurationManager.GetServiceID(m_PaymSubjTp);
                    el_to.AppendChild(el_service_id);

                    XmlElement el_amount_to = XmlClass.CreateElement(xmldoc, "amount", null);
                    el_amount_to.InnerText = m_Amount;
                    el_to.AppendChild(el_amount_to);

                    XmlElement el_account_number = XmlClass.CreateElement(xmldoc, "account-number", null);
                    el_account_number.InnerText = m_paym_params[0];
                    if (m_PaymSubjTp == "420")
                    {
                        el_account_number.InnerText = "1111 " + el_account_number.InnerText + ";123";
                        GlobalObjectsManager.Logger.Info("KSO PROFI FIX el_account_number.InnerText TO: " + el_account_number.InnerText);
                    }

                    el_to.AppendChild(el_account_number);

                    el_method.AppendChild(el_to);

                    xmldoc.SelectSingleNode("request").AppendChild(el_root);
                    break;
                }
        }

        msg = xmldoc.OuterXml;
        return msg;
    }

    private void Response(string msg)
    {
        XmlDocument doc = new XmlDocument();
        doc.LoadXml(msg);

        
        if (doc.LastChild["result-code"] != null)
        {
            int err_result_code = int.Parse(doc.LastChild["result-code"].InnerText);
            if (err_result_code != 0)
            {
                string err = GlobalObjectsManager.GetError(err_result_code);

                string fatal = string.Empty;
                if (doc.LastChild["result-code"].Attributes["fatal"] != null
                && bool.Parse(doc.LastChild["result-code"].Attributes["fatal"].Value) == true)
                    fatal = "fatal;";

                ret_xml = ret_xml.Replace("%code%", "ERROR");
                ret_xml = ret_xml.Replace("%desc%", fatal + "Ошибка. Кошелек " + m_TerminalID + " недоступен. " + err);
                return;
            }
        }

        string desc = string.Empty;
        if (m_Step == enOperation.ResultCodes || m_Step == enOperation.Providers || m_Step == enOperation.Reestr)
        {
            string fn = m_Step.ToString() + ".xml";
            doc.Save(GlobalObjectsManager.curr_path + fn);
            ret_xml = ret_xml.Replace("%code%", "OK");
            ret_xml = ret_xml.Replace("%desc%", "OK");
        }
        else
            if (m_Step == enOperation.Balance)
            {
                XmlNode bal = doc.SelectSingleNode("*/bal");
                string balance = bal.InnerText;
                decimal db = decimal.Parse(balance.Replace('.', ','));
                long lb = (long)(db * 100);
                Balance_Update(m_TerminalID, lb);
                ret_xml = ret_xml.Replace("%code%", "OK");
                ret_xml = ret_xml.Replace("%desc%", lb.ToString());
            }
            else
                if (m_Step == enOperation.CheckStatus || m_Step == enOperation.Payment)
                {
                    string configuration_id = doc.SelectSingleNode("*/configuration-id").InnerText;
                    string BALANCE = doc.SelectSingleNode("*/extra[@name='BALANCE']").InnerText;
                    decimal db0 = decimal.Parse(BALANCE.Replace('.', ','));
                    long lb0 = (long)(db0 * 100);
                    
                    Balance_Update(m_TerminalID, lb0);

                    XmlNode node = doc.SelectSingleNode("*/payment");
                    if (node != null)
                    {
                        string txn_id = node.Attributes["txn_id"].Value;
                        int status = int.Parse(node.Attributes["status"].Value);
                        int result_code = int.Parse(node.Attributes["result-code"].Value);
                        string err_msg = string.Empty; 
                        
                        if(result_code !=0)
                            err_msg = GlobalObjectsManager.GetError(result_code);

                        bool final_status = bool.Parse(node.Attributes["final-status"].Value);
                        bool fatal_error = bool.Parse(node.Attributes["fatal-error"].Value);
                        string txn_date = string.Empty;
                        if (node.Attributes["txn-date"] != null)
                            txn_date = node.Attributes["txn-date"].Value;

                        //0..50
                        //Не обработана
                        //51..59
                        //В обработке
                        //60..99
                        //Обработка завершена успешно
                        //100 >
                        //Обработка завершена с ошибкой

                        if (result_code !=0)
                        {
                            ret_xml = ret_xml.Replace("%code%", "ERROR");
                            ret_xml = ret_xml.Replace("%desc%", "TerminalID=" + m_TerminalID + ";status=" + status + ";result_code=" + result_code + ";txn_id=" + txn_id + ";txn_date=" + txn_date + ";err_msg=" + err_msg);
                        }
                        else
                        {
                            if (!final_status)
                            {
                                if (m_StepLast != enOperation.CheckStatus)
                                {
                                    m_Step = enOperation.CheckStatus;
                                    System.Threading.Thread.Sleep(EtranConfigurationManager.StatusWait);
                                    return;
                                }
                            }

                            ret_xml = ret_xml.Replace("%code%", "OK");
                            ret_xml = ret_xml.Replace("%desc%", "TerminalID=" + m_TerminalID + ";status=" + status + ";result_code=" + result_code + ";txn_id=" + txn_id + ";txn_date=" + txn_date);
                        }
                    }
                    else
                    {
                        if (m_Step == enOperation.CheckStatus)
                        {
                            decimal db1 = decimal.Parse(m_Amount.Replace('.', ','));
                            long lb1 = (long)(db1 * 100);

                            if (lb1 > lb0)
                            {
                                ret_xml = ret_xml.Replace("%code%", "ERROR");
                                ret_xml = ret_xml.Replace("%desc%", "Недостаточно средств на кошельке " + m_TerminalID + ".BALANCE=" + BALANCE);
                            }
                            else
                            {
                                m_Step = enOperation.Payment;
                            }
                        }
                    }
                }

    }

    string Request()
    {
        XmlDocument doc = new XmlDocument();
        string msg = CreateMessage();

        GlobalObjectsManager.Logger.Info("SEND-" + m_PaymExtId + "-" + m_Step.ToString() + "-" + DateTime.Now.ToString("yyyyMMddHHmmss") + ": " + msg);

        //if (m_Step == enOperation.CheckStatus || m_Step == enOperation.Payment)
        //{
        //    doc.LoadXml(msg);
        //    doc.Save(GlobalObjectsManager.curr_path + "LOG\\" + m_PaymExtId + "-SEND-" + m_Step.ToString() + "-" + DateTime.Now.ToString("yyyyMMddHHmmss") + ".xml");
        //}

        if (m_Step == enOperation.Payment)
        {
            DB_PutTerminalID(m_TerminalID, m_PaymExtId);
        }

        System.Net.ServicePointManager.SecurityProtocol = System.Net.SecurityProtocolType.Tls;
        string ret = Net.WebRequest(EtranConfigurationManager.PaymentUrl, m_ConnectionTimeout, msg, null);

        GlobalObjectsManager.Logger.Info("RECV-" + m_PaymExtId + "-" + m_Step.ToString() + "-" + DateTime.Now.ToString("yyyyMMddHHmmss") + ": " + ret);

        //if (m_Step == enOperation.CheckStatus || m_Step == enOperation.Payment)
        //{
        //    doc.LoadXml(ret);
        //    doc.Save(GlobalObjectsManager.curr_path + "LOG\\" + m_PaymExtId + "-RECV-" + m_Step.ToString() + "-" + DateTime.Now.ToString("yyyyMMddHHmmss") + ".xml");
        //}

        return ret;
    }

    public static string GetToken(string number, string pwd, string code, string vcode)
    {
        //string msg = "<?xml version=\"1.0\" encoding=\"utf-8\"?><request><request-type>491</request-type><phone>9162376067</phone><client-id>PLATERRA</client-id></request>";
        //string msg = "<?xml version=\"1.0\" encoding=\"utf-8\"?><request><request-type>491</request-type><phone>9162376067</phone><password>iz1osxy4</password><client-id>PLATERRA</client-id></request>";
        //string msg = "<?xml version=\"1.0\" encoding=\"utf-8\"?><request><request-type>491</request-type><phone>9162376067</phone><password>iz1osxy4</password><client-id>PLATERRA</client-id><code>116de43e551c53f3d8af318913f353</code><vcode>899179</vcode></request>";
        string msg = string.Empty;

	if(pwd==null || pwd.Length == 0 )
            msg = "<?xml version=\"1.0\" encoding=\"utf-8\"?><request><request-type>491</request-type><phone>"+number
                +"</phone><client-id>PLATERRA</client-id></request>";
	else
        if(code!=null && code.Length > 0)
            msg = "<?xml version=\"1.0\" encoding=\"utf-8\"?><request><request-type>491</request-type><phone>"+number
                +"</phone><password>"+pwd+"</password><client-id>PLATERRA</client-id><code>"+code+"</code><vcode>"+vcode+"</vcode></request>";

        else
            msg = "<?xml version=\"1.0\" encoding=\"utf-8\"?><request><request-type>491</request-type><phone>"+number
                +"</phone><password>"+pwd+"</password><client-id>PLATERRA</client-id></request>";


        GlobalObjectsManager.Logger.Info("SEND-GetToken-" + number + "-"  + DateTime.Now.ToString("yyyyMMddHHmmss") + ": " + msg);

        XmlDocument doc = new XmlDocument();

        doc.LoadXml(msg);
        doc.Save(GlobalObjectsManager.curr_path + "LOG\\" + "GetToken-" + number + "-SEND-" + DateTime.Now.ToString("yyyyMMddHHmmss") + ".xml");

        string ret = Net.WebRequest(EtranConfigurationManager.PaymentUrl, 60000, msg, null);

        GlobalObjectsManager.Logger.Info("RECV-GetToken-" + number + "-" + DateTime.Now.ToString("yyyyMMddHHmmss") + ": " + ret);

        doc.LoadXml(ret);
        doc.Save(GlobalObjectsManager.curr_path + "LOG\\" + "GetToken-" + number + "-RECV-" + DateTime.Now.ToString("yyyyMMddHHmmss") + ".xml");

        return ret;
    }


    public string DoRequest()
    {
        try
        {
            //return DoCustomReq();

            switch (m_Function)
            {
                case "reestr":
                    m_Step = enOperation.Reestr;
                    m_TerminalID = m_Accounts.Keys[0];
                    m_PWD = m_Accounts[m_TerminalID];
                    break;
                case "providers":
                    m_Step = enOperation.Providers;
                    m_TerminalID = m_Accounts.Keys[0];
                    m_PWD = m_Accounts[m_TerminalID];
                    break;
                case "resultcodes":
                    m_Step = enOperation.ResultCodes;
                    break;
                case "balance":
                    m_Step = enOperation.Balance;
                    m_TerminalID = m_Accounts.Keys[0];
                    m_PWD = m_Accounts[m_TerminalID];
                    break;
                case "payment":
                    string tid = GetTerminalID();
                    if (tid != null && tid.Length > 0)
                    {
                        m_TerminalID = tid;
                        m_PWD = m_Accounts[tid];
                        GlobalObjectsManager.Logger.Info("Установили m_TerminalID=" + m_TerminalID + " m_PWD=" + m_PWD);
                    }
                    else
                    {
                        throw new Exception("Не удалось установить m_TerminalID");
                    }

                    m_Step = enOperation.CheckStatus;
                    break;
            }

            while (m_Step != m_StepLast)
            {
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
            ret_xml = ret_xml.Replace("%desc%", "ERROR");
        }
        finally
        {
            GlobalObjectsManager.Logger.Info("m_PaymExtId: " + m_PaymExtId + " ret_xml: " + ret_xml);
        }
        return ret_xml;
    }
}
