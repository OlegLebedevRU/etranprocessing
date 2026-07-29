using System;
using System.Net;
using System.IO;
using System.Text;
using System.Xml;
using System.Diagnostics;
using System.Collections;
using System.Threading;
using Microsoft.Win32;
using System.Runtime.InteropServices;
using RemotingInterfaces;
using System.Reflection;
using CybServer;
using org.CyberPlat;


namespace CyberInterface
{
    public enum eMobile { id = 0, sum = 1, tsp_code = 2, param = 3 };

    public class CyberplatClass : MarshalByRefObject, IDataExchange
    {

        string m_sLastError = null;
        string m_sMsg_tmpl = "";
        string m_sOperators_file_name = "";
        string m_sSecret_file_name = "";
        string m_sSecret_password = "";
        string m_sPublic_file_name = "";
        string m_sPublic_serial = "";
        ArrayList[] m_error_list = null;
        public static string m_Exe_path = null;
        const string cyb_config_name = "Cyberplat.xml";
        XmlDocument m_operators_xml = new XmlDocument();
        IPrivKey sec = null;
        IPrivKey pub = null;


        public override object InitializeLifetimeService()
        {
            return null;
        }

        public string GetLastError()
        {
            string msg = null;
            if (m_sLastError != null && m_sLastError.Length > 0)
            {
                msg = m_sLastError;
                m_sLastError = null;
            }
            return msg;
        }


        void SetLastError(int id)
        {
            m_sLastError = "UNKNOWN ERROR CODE [" + id + "]";
            for (int i = 0; i < m_error_list.Length; i++)
            {
                if (id == int.Parse((string)m_error_list[i][0]))
                {
                    m_sLastError = (string)m_error_list[i][1];
                }

            }
        }


        public CyberplatClass()
        {
            clsLogger.Instance().SaveInfo("CyberplatClass START");

            try
            {
                string config_name = m_Exe_path + cyb_config_name;
                XmlDocument doc = new XmlDocument();
                doc.Load(config_name);

                string path_err = doc.SelectSingleNode("/config/log").InnerText;
                m_sMsg_tmpl = doc.SelectSingleNode("/config/msg").InnerText;
                m_sMsg_tmpl = m_sMsg_tmpl.Replace("%0D%0A", "\r\n");
                m_sOperators_file_name = doc.SelectSingleNode("/config/operators").InnerText;

                XmlNodeList nodeList = doc.SelectNodes("/config/error/add");
                int i = 0;
                m_error_list = new ArrayList[nodeList.Count];

                while (i < nodeList.Count)
                {
                    m_error_list[i] = new ArrayList();
                    m_error_list[i].Add(nodeList[i].Attributes["id"].Value);
                    m_error_list[i].Add(nodeList[i].Attributes["Description"].Value);
                    i++;
                }


                XmlNode node = null;

                node = doc.SelectSingleNode("/config/keys/secret");
                m_sSecret_file_name = node.InnerText;
                m_sSecret_password = node.Attributes.GetNamedItem("password").Value;

                node = doc.SelectSingleNode("/config/keys/public");
                m_sPublic_file_name = node.InnerText;
                m_sPublic_serial = node.Attributes.GetNamedItem("serial").Value;

                IPriv.Initialize();
                sec = IPriv.openSecretKey(m_sSecret_file_name, m_sSecret_password);
                pub = IPriv.openPublicKey(m_sPublic_file_name, Convert.ToUInt32(m_sPublic_serial, 10));

                // try load operators.xml - > start
                clsLogger.Instance().SaveInfo("try load operators.xml - > start");
                m_operators_xml.Load(m_sOperators_file_name);
            }
            catch (Exception ex)
            {
                clsLogger.Instance().SaveError(ex);
                throw;
            }
            clsLogger.Instance().SaveInfo("CyberplatClass DONE");
        }




        string DeCodeMessage(ref string src)
        {
            string data = "";
            string code = "";
            string head = "inputmessage=";
            data = src.Remove(0, head.Length);
            data = data.Replace('+', ' ');
            for (int i = 0; i < data.Length; i++)
            {
                if (data[i] == '%')
                {
                    string val = (data[++i].ToString() + data[++i].ToString());
                    code += (char)Convert.ToByte(val, 16);
                }
                else
                    code += data[i];
            }
            return code;
        }


        string CodeMessage(ref string data)
        {
            string result = "inputmessage=";
            for (int i = 0; i < data.Length; i++)
            {
                if (!char.IsLetterOrDigit(data[i]))
                {
                    byte code_byte = (byte)data[i];

                    if (code_byte == 32)
                        result += "+";
                    else
                    {
                        string val = "";
                        if (code_byte < 16)
                            val = string.Format("%0{0:x}", code_byte);
                        else
                            val = string.Format("%{0:x}", code_byte);
                        val = val.ToUpper();
                        result += val;
                    }
                }
                else
                    result += data[i];
            }
            return result;
        }

        bool CodeAnalysis(string code)
        {
            int err_code = 0;
            string h = code.Remove(0, code.IndexOf("ERROR") + "ERROR".Length);
            if (h.IndexOf('\r') == -1) return false;
            string dig = "";
            int i = 0;
            while (!char.IsDigit(h, i) && i < h.Length)
                if (h[i++] == '\r') return false;
            while (char.IsDigit(h, i) && i < h.Length)
                dig += h[i++];
            err_code = int.Parse(dig);
            if (err_code != 0)
                SetLastError(err_code);

            return (err_code == 0) ? true : false;
        }



        public bool Encode(string[] par, int mode, out string url, out string msg) // "0","1", "2"
        {
            lock (this)
            {
                clsLogger.Instance().SaveInfo("Encode>> mode " + mode + string.Format(" id {0}, sum {1}, tsp {2}, param {3}" , par[0], par[1], par[2], par[3]));
                url = null;
                msg = null;
                try
                {

                    string function = string.Empty;
                    switch (mode)
                    {
                        case 1:
                            function = "check";
                            break;
                        case 2:
                            function = "pay";
                            break;
                        case 3:
                            function = "status";
                            break;

                    }
                    OperatorsMessage om = new OperatorsMessage(m_sMsg_tmpl, function, par[2], par[0], par[1], par[3]);
                    om.CreatePacket(ref m_operators_xml);
                    string Msg = om.m_packet;
                    string SignedMsg = sec.signText(Msg);
                    clsLogger.Instance().SaveInfo("Encode>> SignMsg " + SignedMsg);
                    msg = CodeMessage(ref SignedMsg);
                    url = om.m_url;
                    clsLogger.Instance().SaveInfo("Encode>> url " + url + " CodeMessage " + msg);

                }
                catch (Exception ex)
                {
                    clsLogger.Instance().SaveError("Encode>>", ex);
                }
                return true;
            }
        }

        public bool Decode(string msg, out string resp)
        {
            lock (this)
            {
                resp = string.Empty;
                try
                {
                    clsLogger.Instance().SaveInfo("Decode>> msgIn " + msg);
                    pub.verifyText(msg);
                    int IndxStart = msg.IndexOf("BEGIN") + "BEGIN".Length;
                    int Length = msg.IndexOf(Environment.NewLine +  "END") - IndxStart;
                    resp = msg.Substring(IndxStart, Length);
                    clsLogger.Instance().SaveInfo("Decode OK >>" + resp);

                    return CodeAnalysis(msg);
                }
                catch (Exception ex)
                {
                    clsLogger.Instance().SaveError("Decode>>", ex);
                }
                return false;
            }
        }
    }
}
