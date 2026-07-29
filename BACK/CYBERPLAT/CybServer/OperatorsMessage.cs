using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Text;
using System.Xml;



namespace CybServer
{
    public class OperatorsMessage
    {
        public string m_url;
        //public string m_params;
        public string m_tsp_code;
        public string m_paymextid;
        public string m_amount;
        public string m_packet;
        public string m_packet_type;
        public NameValueCollection m_nvc_params = new NameValueCollection();
        //public string[] param_value;
        //public string[] param_code;

        static public NameValueCollection GetNameValueCollection(string Params)
        {
            NameValueCollection paramsTable = new NameValueCollection();
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


        public OperatorsMessage(string msg_head, string function, string tsp, string paymextid, string amount, string param)
        {
            m_packet_type = function;
            m_tsp_code = tsp;
            m_paymextid = paymextid;
            m_paymextid = m_paymextid.Replace(" ", "");
            m_paymextid = m_paymextid.Replace("_", "A");
            m_paymextid = m_paymextid.Replace("-", "B");
            m_packet = msg_head.Replace("%id%", m_paymextid);
            m_amount = (double.Parse(amount) / 100).ToString();
            m_amount = m_amount.Replace(',', '.');

            //m_params = param;
            m_nvc_params = GetNameValueCollection(param);
            //param_value = this.GetParam(';', ' ');
            //param_code = this.GetParam(' ', ';');
        }

        //public int GetCountParams()
        //{
        //    int count = 0;
        //    if (m_params != null && m_params.Length > 0)
        //    {
        //        count = 1;
        //        for (int i = 0; i < m_params.Length; i++)
        //        {
        //            if (m_params[i] == ';')
        //                count++;
        //        }
        //    }
        //    return count;
        //}


        public string Inverse(string t)
        {
            string temp = null;
            if (t != null && t.Length > 0)
            {
                if (t.Length == 1)
                    return t;
                for (int i = t.Length - 1; i >= 0; i--)
                    temp += t[i];
            }
            return temp;

        }

        //public string[] GetParam(char s1, char s2)
        //{
        //    int count = GetCountParams();
        //    string[] parameter_code = new string[count];

        //    if (m_params != null && m_params.Length > 0)
        //    {
        //        int indx_code_start = 0;
        //        for (int i = 0; i < count && indx_code_start + 1 < m_params.Length; i++)
        //        {
        //            int indx_code_start_i = 0;
        //            indx_code_start = m_params.IndexOf(s1, indx_code_start + 1);
        //            if (indx_code_start == -1)
        //                indx_code_start = m_params.Length;

        //            indx_code_start_i = indx_code_start - 1;
        //            while (indx_code_start_i >= 0 && m_params[indx_code_start_i] != s2)
        //            {
        //                parameter_code[i] += m_params[indx_code_start_i];
        //                indx_code_start_i--;

        //            }
        //            parameter_code[i] = Inverse(parameter_code[i]);
        //        }
        //    }
        //    return parameter_code;
        //}

        public void CreatePacket(ref XmlDocument operators_xml)
        {

            m_url = operators_xml.SelectSingleNode("/operators/config/server").InnerText;

            XmlNode node = null;
            node = operators_xml.SelectSingleNode("/operators/operator");

            for (; node.SelectSingleNode("id").InnerText != m_tsp_code && node != null; )
                node = node.NextSibling;

            m_url += node.SelectSingleNode(m_packet_type + "_url").InnerText;
            XmlNodeList packets_list = node.SelectNodes("packets/packet");
            int i = 0;
            string f_type = (m_packet_type == "status") ? "check" : m_packet_type;
            for (i = 0; i < packets_list.Count; i++)
            {
                if (packets_list[i].Attributes["type"].Value == f_type)
                {
                    m_packet += packets_list[i].InnerText.Replace("\\r\\n", "\r\n");
                    break;
                }
            }

            m_packet = m_packet.Replace("{amount}", m_amount);
            XmlNodeList fields_list = node.SelectNodes("fields/field");
            int len = m_nvc_params.Count; //param_code.Length;

            for (int k = 0; k < len; k++)
            {
                for (i = 0; i < fields_list.Count; i++)
                {
                    //if (fields_list[i].SelectSingleNode("id").InnerText == param_code[k])
                    if (fields_list[i].SelectSingleNode("id").InnerText == m_nvc_params.Keys[k])
                    {
                        string name = fields_list[i].SelectSingleNode("name").InnerText;
                        m_packet = m_packet.Replace("{" + name + "}", m_nvc_params[k]);
                    }
                }
            }

            // cleanup unused fields
            for (i = 0; i < m_packet.Length; i++)
                if (m_packet[i] == '{')
                    m_packet = m_packet.Remove(i, m_packet.IndexOf('}', i) - i + 1);


        }

    }
}
