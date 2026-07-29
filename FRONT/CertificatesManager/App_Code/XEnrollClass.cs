using System;
using CERTCLIENTLib;
using CERTADMINLib;
using System.Security.Cryptography.X509Certificates;
using System.Xml;
using System.Data;


namespace XEnrollService
{
    /// <summary>
    /// Summary description for Class1.
    /// </summary>
    class XEnrollClass
    {

        public enum eTypeResponses { OK, ERROR, EXCEPTION, DEFAULT };

        static public string MakeXmlResponse(eTypeResponses tp, string msg, int code)
        {
            string response = null;
            switch (tp)
            {
                case eTypeResponses.OK:
                    response = "<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>OK</Result>" + "<code>" + code + "</code>";
                    response += "<CERTDATA>";
                    response += msg;
                    response += "</CERTDATA>";
                    response += "</Response>";
                    break;
                case eTypeResponses.ERROR:
                    response = "<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>Error</Result>" + "<code>" + code + "</code>";
                    //response += "<Description>При обработке запроса произошла ошибка: ";
                    response += "<Description>";
                    response += msg;
                    response += "</Description></Response>";
                    break;
                case eTypeResponses.EXCEPTION:
                    response = "<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>Error</Result>" + "<code>" + code + "</code>";
                    response += "<Description>При обработке запроса произошла системная ошибка: ";
                    response += msg;
                    response += "</Description></Response>";
                    break;
                case eTypeResponses.DEFAULT:
                    response = "<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>Error</Result>" + "<code>" + code + "</code>";
                    response += "<Description>При обработке запроса произошла непредвиденная ошибка: ";
                    response += "default";
                    response += "</Description></Response>";
                    break;

            }
            return response;
        }


        static public string NetReq(string msg)
        {
            string to_send = EtranConfigurationManager.RootCAUrl + "?" + msg;
            GlobalObjectsManager.Logger.Info("NetReq to_send: " + to_send);
            string ret = (new System.Net.WebClient()).DownloadString(to_send);
            GlobalObjectsManager.Logger.Info("NetReq ret: " + ret);
            return ret;
        }

        static public string CACertIssue(string pkcs10, out string serial_number)
        {
            serial_number = string.Empty;
            GlobalObjectsManager.Logger.Info("CACertIssue START...");
            //CEnrollClass certEnroll = new CEnrollClass();
            CCertRequestClass certRequest = new CCertRequestClass();
            CCertConfigClass certConfig = new CCertConfigClass();

            string config = certConfig.GetConfig(0);
            const int CR_IN_BASE64 = 0x1;
            //const int CR_IN_PKCS10 = 0x100;
            const int CR_OUT_CHAIN = 0x100;
            const int CR_IN_FORMATANY = 0;

            GlobalObjectsManager.Logger.Info("config: " + config);
            //int result = certRequest.Submit(CR_IN_BASE64 | CR_IN_PKCS10, pkcs10, "", config);
            int result = certRequest.Submit(CR_IN_BASE64 | CR_IN_FORMATANY, pkcs10, "CertificateTemplate:" + EtranConfigurationManager.CertificateTemplate, config);

            GlobalObjectsManager.Logger.Info("result submit: " + result);

            CCertAdminClass certAdmin = new CCertAdminClass();
            certAdmin.ResubmitRequest(config, certRequest.GetRequestId());

            int id = certRequest.GetRequestId();
            serial_number = id.ToString();
            certRequest.RetrievePending(id, config);
            const int CR_OUT_BASE64 = 0x01;
            string issuedCert = certRequest.GetCertificate(CR_OUT_BASE64 | CR_OUT_CHAIN);
            return issuedCert;
        }

        static public string GetCertificateProxy(string pin, string pkcs10, string cpserial, string tosign)
        {
            string result = MakeXmlResponse(eTypeResponses.DEFAULT, null, 0);
            try
            {
                int code = 1;
                string Description = "";

                string req_ret = NetReq("function=check&pin=" + pin);
                XmlDocument doc = new XmlDocument();
                doc.LoadXml(req_ret);
                if (doc.SelectSingleNode("Response/Result").InnerText.ToLower() == "ok")
                {
                    string serial_number = string.Empty;
                    string cert = XEnrollClass.CACertIssue(pkcs10, out serial_number);
                    GlobalObjectsManager.Logger.Info("CACertIssue RET:" + cert);
                    GlobalObjectsManager.Logger.Info("TRY REG CERT INTO DB...");
                    req_ret = NetReq("function=dbsetup&pin=" + pin + "&serial=" + serial_number + "&cpserial=" + cpserial + "&ca_serial=" + GlobalObjectsManager.CASerial);
                    GlobalObjectsManager.Logger.Info("dbsetup: " + req_ret);
                    doc.LoadXml(req_ret);
                    if (doc.SelectSingleNode("Response/Result").InnerText.ToLower() == "ok")
                    {
                        return MakeXmlResponse(eTypeResponses.OK, cert, 0);
                    }
                }
                code = int.Parse(doc.SelectSingleNode("Response/code").InnerText);
                if (doc.SelectSingleNode("Response/Description")!=null)
                    Description = doc.SelectSingleNode("Response/Description").InnerText;
                result = MakeXmlResponse(eTypeResponses.ERROR, Description, code);
            }
            catch (Exception e)
            {
                GlobalObjectsManager.Logger.Error(e);
                result = MakeXmlResponse(eTypeResponses.ERROR, e.Message, 1);
            }
            return result;
        }

        static public string GetCertificate(string login, string pwd, string pin, string pkcs10, string cpserial, string tosign)
        {
            GlobalObjectsManager.Logger.Info("GetCertificate login: " + login + " pwd: " + pwd + " pin: " + pin + " pkcs10: " + pkcs10 + " cpserial: " + cpserial + " tosign: " + tosign);

            string result = MakeXmlResponse(eTypeResponses.DEFAULT, null, 0);
            try
            {
                DataSet ds = null;
                if(login != null)
                    ds = DBInterface.UserAutho(login, pwd, pin);
                else
                    ds = DBInterface.Autho(pin, tosign);

                string db_res = Convert.ToString(ds.Tables[0].Rows[0]["result"]);
                GlobalObjectsManager.Logger.Info("GetCertificate db_res: " + db_res);

                int code = 1;
                if (ds.Tables[0].Rows[0]["code"] != null)
                    code = int.Parse(ds.Tables[0].Rows[0]["code"].ToString());

                if (db_res.ToLower() == "ok")
                {
                    string serial_number = string.Empty;
                    string cert = CACertIssue(pkcs10, out serial_number);
                    GlobalObjectsManager.Logger.Info("cert: " + cert);
                    GlobalObjectsManager.Logger.Info("serial_number: " + serial_number);
                    

                    if (login != null)
                        ds = DBInterface.UserSetup(login, pwd, pin, serial_number);
                    else
                        ds = DBInterface.Setup(pin, serial_number, cpserial, GlobalObjectsManager.CASerial);

                    db_res = Convert.ToString(ds.Tables[0].Rows[0]["result"]);
                    if (db_res.ToLower() == "ok")
                    {
                        return MakeXmlResponse(eTypeResponses.OK, cert, 0);
                    }
                    else
                    {
                        if (ds.Tables[0].Rows[0]["code"] != null)
                            code = int.Parse(ds.Tables[0].Rows[0]["code"].ToString());
                    }
                }
                result = MakeXmlResponse(eTypeResponses.ERROR, Convert.ToString(ds.Tables[0].Rows[0]["descr"]), code);
            }
            catch (Exception e)
            {
                GlobalObjectsManager.Logger.Error(e);
                result = MakeXmlResponse(eTypeResponses.ERROR, e.Message, 1);
            }
            return result;
        }

        static public string DbSetup(string pin, string serial, string cpserial, string ca_serial)
        {

            string result = MakeXmlResponse(eTypeResponses.DEFAULT, null, 0);
            try
            {
                DataSet ds = DBInterface.Setup(pin, serial, cpserial, ca_serial);
                string db_res = Convert.ToString(ds.Tables[0].Rows[0]["result"]);
                int code = 1;
                if (ds.Tables[0].Rows[0]["code"] != null)
                    code = int.Parse(ds.Tables[0].Rows[0]["code"].ToString());

                if (db_res.ToLower() == "ok")
                {
                    return MakeXmlResponse(eTypeResponses.OK, "DBOK", 0);
                }
                result = MakeXmlResponse(eTypeResponses.ERROR, Convert.ToString(ds.Tables[0].Rows[0]["descr"]), code);
            }
            catch (Exception e)
            {
                GlobalObjectsManager.Logger.Error(e);
                result = MakeXmlResponse(eTypeResponses.ERROR, e.Message, 1);
            }
            return result;
        }

    }
}






