using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Configuration;
using System.Data;
using System.Web;
using System.Web.Services;
using System.Web.Services.Protocols;

using System.Collections.Specialized;
using System.Text;
using System.Net;
using System.IO;
using System.Xml;
using System.Runtime.Remoting;
//using System.Runtime.Remoting.Channels.Http;
using System.Runtime.Remoting.Channels;
using RemotingInterfaces;



namespace SysCyberPlat
{
    /// <summary>
    /// Сводное описание для Service1
    /// </summary>
    [WebService(Namespace = "http://tempuri.org/")]
    [WebServiceBinding(ConformsTo = WsiProfiles.BasicProfile1_1)]
    [ToolboxItem(false)]
    public class Service1 : System.Web.Services.WebService
    {

        public enum eMobile { id = 0, sum = 1, tsp_code = 2, param = 3 };

        IDataExchange obj;
        static int m_ServerTimeOut = Convert.ToInt32(System.Configuration.ConfigurationManager.AppSettings["ServerTimeOut"]);
        static string PlaterraUrl = System.Configuration.ConfigurationManager.AppSettings["PlaterraUrl"];

        bool function(string[] par, int mode, int timeout, out string resp)
        {
            string url = null;
            string msg = null;
            //string count_total="";
            string sid = par[0];
            bool result;
            try
            {
                //int count_start =Environment.TickCount;
                //			GlobalObjectsManager.Logger.Info("url "+url+" msg "+msg);
                bool ret = obj.Encode(par, mode, out url, out msg);
                //int count_end =Environment.TickCount;
                //count_total += "[ Encode == " +(count_end - count_start) +" ] " ;

                //count_start =Environment.TickCount;
                GlobalObjectsManager.Logger.Info("{S} ID: " + sid + " url " + url + " timeout " + timeout.ToString() + " msg " + msg);
                string web_ret = WebRequest(url, msg, timeout);
                GlobalObjectsManager.Logger.Info("{R} ID: " + sid + " web_ret " + web_ret);

                string s_end = "END SIGNATURE";
                // отрезаем что ненужное
                int indx_start = web_ret.IndexOf(s_end);
                if (indx_start > 0 && web_ret.Length > (indx_start + s_end.Length))
                {
                    GlobalObjectsManager.Logger.Info("{REMOVE TRY} ID: " + sid);
                    web_ret = web_ret.Remove(indx_start + s_end.Length);
                    GlobalObjectsManager.Logger.Info("{REMOVE DONE} ID: " + sid + " MSG: " + web_ret);

                }
                //count_end =Environment.TickCount;
                //count_total += "[ WebRequest == "+ (count_end - count_start) +" ] " ;

                //count_start =Environment.TickCount;
                result = obj.Decode(web_ret, out resp);
                //count_end =Environment.TickCount;
                //count_total += "[ Decode == " +(count_end - count_start) +" ] " ;

                GlobalObjectsManager.Logger.Info("{DECODE} ID: " + sid + " RES: " + resp);
            }
            catch (Exception e)
            {
                GlobalObjectsManager.Logger.Error("{EXCEPTION} ID: " + sid + " EXC: " + e.Message);
                throw new Exception();

            }
            return result;
        }

        string WebRequest(string url, string sign_msg, int timeout)
        {
            System.Net.ServicePointManager.SecurityProtocol = System.Net.SecurityProtocolType.Tls12;
            byte[] data = Encoding.Default.GetBytes(sign_msg);
            HttpWebRequest wrq = HttpWebRequest.Create(url) as HttpWebRequest;
            wrq.Timeout = timeout;
            wrq.Method = "POST";
            wrq.ContentType = "application/x-www-form-urlencoded";
            wrq.ContentLength = data.Length;
            Stream newStream = wrq.GetRequestStream();
            newStream.Write(data, 0, data.Length);
            newStream.Close();
            HttpWebResponse hwr = wrq.GetResponse() as HttpWebResponse;
            Stream strm = hwr.GetResponseStream();
            StreamReader reader = new StreamReader(strm, Encoding.GetEncoding(1251));
            string res = reader.ReadToEnd();
            return res;
        }

        private string GetResponseError(string PaymExtId)
        {
            string xml_tmpl_resp =
    @"<Response>
<Result>%code%</Result>
<PaymExtId>%id%</PaymExtId>
<Description>%desc%</Description>
</Response>";

            xml_tmpl_resp = xml_tmpl_resp.Replace("%id%", PaymExtId);
            xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "ERROR");
            xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", "inner error");
            return xml_tmpl_resp;
        }


        [WebMethod]
        public void Test()
        {
            TspExtraParams TspExtraParams;
            try
            {
                TspExtraParams r = new TspExtraParams();
                var term = new List<Terminal>();
                var items = new List<Item>();
                items.Add(new Item() { Id = "2001", Name = "ORGNAME", Value = "УФК по РБ (МВД по РБ)" });
                items.Add(new Item() { Id = "2002", Name = "INN", Value = "0275006462" });
                items.Add(new Item() { Id = "2003", Name = "ACCOUNT", Value = "37||80701000" });

                term.Add(new Terminal() { Id = ",175,193,", Items = items });

                
                var items1 = new List<Item>();
                items1.Add(new Item() { Id = "2001", Name = "ORGNAME", Value = "УФК по РБ (УМВД России по городу Уфе)" });
                items1.Add(new Item() { Id = "2002", Name = "INN", Value = "0276011698" });
                items1.Add(new Item() { Id = "2003", Name = "ACCOUNT", Value = "37||80701000" });

                term.Add(new Terminal() { Id = "192", Items = items1 });


                r.tsp = new List<Tsp>();
                r.tsp.Add(new Tsp() { Id = "707", Terminals = term });

                XmlDocument doc = new XmlDocument();
                doc.LoadXml(XmlService.Serialize(r));
                doc.Save(GlobalObjectsManager.curr_path + "1111.xml");
                //var t = XmlService.Serialize(r);
                doc.Load(GlobalObjectsManager.curr_path + "1111.xml");
                TspExtraParams = XmlService.DeSerialize<TspExtraParams>(doc.OuterXml);
                
                int count  = TspExtraParams.tsp.Count;

                //
                int yu = 0;

            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error(ex);
            }
        }

        [WebMethod]
        public long GetAmount(string Rek)
        {
            try
            {
                if (Rek != null && Rek.Length > 1)
                {
                    NameValueCollection m_Rek = GetNameValueCollection(Rek, ";", "=");
                    Rek = (m_Rek["url"] == null) ? Rek : m_Rek["url"];
                }
                else
                    Rek = System.Configuration.ConfigurationManager.AppSettings["PlaterraUrl"];

                string response = EtranDoRequest("check", "1", "1", "1", "1", 60, Rek, "1");
                response = "<?xml version = \"1.0\" encoding = \"windows-1251\"?>" + response;
                XmlDocument doc = new XmlDocument();
                doc.LoadXml(response);
                response = doc.SelectSingleNode("/Response/Description").InnerText;
                response = response.Substring(response.IndexOf("REST=") + "REST=".Length);
                int indx = response.IndexOf('<');
                if (indx > 0)
                    response = response.Remove(indx, response.Length - indx);
                GlobalObjectsManager.Logger.Info(string.Format("response : " + response));
                response = response.Replace('.', ',');
                long amount = 0;
                double d = double.Parse(response);
                if (d > 0)
                {
                    d = d * 100;
                    amount = (long)d;
                    return amount;
                }
            }
            catch (Exception e)
            {
                GlobalObjectsManager.Logger.Error(e);
            }

            return 0;
        }

        static public NameValueCollection GetNameValueCollection(string data, string delimiter, string split)
        {
            string[] m = data.Split(delimiter.ToCharArray(0, delimiter.Length));
            NameValueCollection result = new NameValueCollection();
            foreach (string s in m)
            {
                string[] m1 = s.Split(split.ToCharArray(0, split.Length));
                if (m1.Length != 2) continue;
                result.Add(m1[0].Trim(), m1[1]);
            }
            return result;
        }

        int RoundUp(int toRound)
        {
            if (toRound % 10 > 0)
                return (10 - toRound % 10) + toRound;
            return toRound;
        }


        [WebMethod]
        public string DoRequest(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek, string TotalSum)
        //public string DoRequest()
        {
            //string Function = "check";
            //string PaymExtId = "0668_170611_12335188";
            //string PaymSubjTp = "6";
            //string Amount = "49508";
            //string Params = "1 34702602";
            ////string Params = "5 МАГНИТОГОРСК, СОВЕТСКАЯ 199/2 , КВ 50;4 ЛЬВОВНА;3 АРИНА;2 ТИХОНОВА;14 9162376067";// "1 2920373669;4 0310;5 0;6 12";
            //int ConnectionTimeout = 60;
            //string Rek = "";
            ////string Rek = "url=http://bl.corepay.local:9957/cyberplat/cybermonsterex;terminal=612;pdt=2010-04-15 02:42:31";
            ////"url=http://bl.corepay.local:9957/cyberplat/cybermonsterex;terminal=612;pdt=2010-04-15 02:42:31";
            ////Rek:0197;terminal=612;pdt=2010-04-15 02:42:31
            //string TotalSum = "500";


            //var roundedA = Math.Round(1.1, 0); // Output: 1
            //var roundedB = Math.Round(1.5, 0, MidpointRounding.AwayFromZero); // Output: 2
            //var roundedC = Math.Round(1.9, 0); // Output: 2

            //var paym_amount = 308176;
            //double fractionalNumber = (double)(paym_amount/100)/100;
            //var wholeNumber = Math.Round(fractionalNumber, 2, MidpointRounding.AwayFromZero); // Output: 2
            //var wholeNumber1 = (int)Math.Ceiling(fractionalNumber);
            //var rub = (int)(fractionalNumber*100);
            //var wholeNumber2 = RoundUp(rub);


            //return "";

            //string Function = "check";
            //string PaymExtId = "0668_170611_12335188";
            //string PaymSubjTp = "707";
            //string Amount = "308176";
            //string Params = "3 18;6 Выдача ВУ российского образца;5 2000;1 АКАТЬЕВ МИХАИЛ АЛЕКСАНДРОВИЧ;2 Г.УФА УЛ.КОММУНИСТИЧЕСКАЯ 136\\1;7 4500123456";
            //int ConnectionTimeout = 60;
            //string Rek = "0197;terminal=175;pdt=2010-04-15 02:42:31";
            //string TotalSum = "1000";

            //if (TotalSum == null)
            //    TotalSum = "0";

            //2016-03-30 06:58:42,742 - Function: payment PaymExtId: 0193_300316_08591422 PaymSubjTp:707 Amount: 200000 Params: 3 18;6 Выдача ВУ российского образца;5 2000;1 АКАТЬЕВ МИХАИЛ АЛЕКСАНДРОВИЧ;2 Г.УФА УЛ.КОММУНИСТИЧЕСКАЯ 136\1;2001 УФК по РБ (МВД по РБ);2002 0275006462;2003 37||80701000 ConnectionTimeout: 90 Rek:http://bl.corepay.local:9971/cyberplat/ModTech TotalSum:2100

            //            2010-04-16 12:44:23,612 - Function: payment PaymExtId: 0841_150410_12125265 PaymSubjTp:6 Amount: 21000 Params: 5 МАГНИТОГОРСК, СОВЕТСКАЯ 199/2 , КВ 50;4 ЛЬВОВНА;3 АРИНА;2 ТИХОНОВА;1 34315714 ConnectionTimeout: 90 Rek:http://bl.corepay.local:9957/cyberplat/cybermonsterex TotalSum:
            //210
            //2010-04-16 12:44:23,612 - 2Params: 5 МАГНИТОГОРСК, СОВЕТСКАЯ 199/2 , КВ 50;4 ЛЬВОВНА;3 АРИНА;2 ТИХОНОВА;1 34315714;1111 210

            NameValueCollection m_Rek = GetNameValueCollection(Rek, ";", "=");

            // fix for easypay transactions
            GlobalObjectsManager.Logger.Info("LEN PaymExtId: " + PaymExtId + " : " + PaymExtId.Length);
            if (PaymExtId.Length == 10)
            {
                DateTime dt = DateTime.Parse(m_Rek["pdt"]);
                string NEWPaymExtId = PaymExtId + dt.ToString("yyyyMMddHH");
                GlobalObjectsManager.Logger.Info("FIX NEWPaymExtId: " + NEWPaymExtId + " : " + NEWPaymExtId.Length);
                PaymExtId = NEWPaymExtId;
            }
            bool fix = false;

            try
            {
//                для тсп 707 и 726
//в транзе с терема прилетает теперь новый параметр -серия и номер паспорта, код этого параметра 7(в обоих тсп)
//прилетает слитно, вот так - 4500123456.
//Нужно в киберовский шлюз 3196 добавить параметр PAYER_DOC = 21 || 4507 || 123456
//где 21 - это тип документа паспорт, 4507 - первые 4 символа(серия), 123456 - правые 6 символов - номер паспорта.

                if (PaymSubjTp == "707" || PaymSubjTp == "726")
                {
                    GlobalObjectsManager.Logger.Info("start add  PAYER_DOC");
                    NameValueCollection param = GetNameValueCollection(Params);

                    string payer = param["7"];
                    if(payer!=null)
                    {
                        GlobalObjectsManager.Logger.Info("start add  PAYER_DOC payer " + payer);
                        payer = "21||" + payer.Insert(4, "||");
                        GlobalObjectsManager.Logger.Info("start add  PAYER_DOC payerFix " + payer);

                        param["7"] = payer;
                        string newParam = string.Empty;
                        foreach (string key in param.AllKeys)
                        {
                            newParam += ";" + key + " " + param[key];
                        }
                        newParam = newParam.Remove(0, 1);
                        Params = newParam;
                    }
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error(ex);
            }

            try
            {
                var nvc = GlobalObjectsManager.CheckTspExtraParams(m_Rek["terminal"], PaymSubjTp);
                if (nvc.Count > 0)
                {
                    fix = true;
                    foreach (var key in nvc.AllKeys)
                    {
                        Params += ";" + key + " " + nvc[key];
                    }
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error(ex);
            }

            if (fix)
                GlobalObjectsManager.Logger.Info("EXTRA Params: " + Params);


            GlobalObjectsManager.Logger.Info("Agent Rek: " + Rek);
            string AgentUrl = m_Rek["url"];
            if (AgentUrl != null && PlaterraUrl != null)
                throw new Exception("Присутствуют оба рекизита.");
            else
                if (AgentUrl != null)
                    Rek = AgentUrl;
                else
                    Rek = PlaterraUrl;


            // только для ПЛАТЕРРЫ смотрим на NoDopKomGate
            if (Rek == PlaterraUrl && Rek != null)
            {
                
                string url_rek = GlobalObjectsManager.Check_NoDopKomGate(PaymSubjTp);

                if (url_rek != null && url_rek.Length > 0)
                {
                    GlobalObjectsManager.Logger.Info(PaymExtId + " tsp founf for NoDopKomGate");
                    long lAmount = long.Parse(Amount);
                    long lTotalSum = long.Parse(TotalSum) * 100;
                    GlobalObjectsManager.Logger.Info(PaymExtId + " check for NoDopKomGate lAmount " + lAmount + " lTotalSum " + lTotalSum);
                    if (lAmount == lTotalSum)
                    {
                        GlobalObjectsManager.Logger.Info(PaymExtId + "check for NoDopKomGate OK url" + url_rek);
                        if (url_rek != null && url_rek.Length > 0)
                            Rek = url_rek;
                    }
                    else
                    {
                        GlobalObjectsManager.Logger.Info(PaymExtId + " NoDopKomGate NOT FOUND");
                    }
                }
            }

            string resp = EtranDoRequest(Function, PaymExtId, PaymSubjTp, Amount, Params, ConnectionTimeout, Rek, TotalSum);

            if (resp.IndexOf("ERROR=30") > 0 || resp.IndexOf("Общая ошибка") > 0)
            {
                //GlobalObjectsManager.Logger.Info("BE: " + resp);
                try
                {
                    XmlDocument doc = new XmlDocument();
                    doc.LoadXml(resp);
                    //doc.SelectSingleNode("Response/Description").InnerText = "Требуется изменить номер транзакции.";
                    doc.SelectSingleNode("Response/Description").InnerText = "Технологический перерыв.";
                    resp = doc.OuterXml;
                }
                catch(Exception e)
                {
                }
                //GlobalObjectsManager.Logger.Info("AF: " + resp);
            }

            return resp;
        }

        static private NameValueCollection GetNameValueCollection(string Params)
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


        private string EtranDoRequest(string Function, string PaymExtId, string PaymSubjTp, string Amount, string Params, int ConnectionTimeout, string Rek, string TotalSum)
        {
            if (GlobalObjectsManager.Logger.IsInfoEnabled)
            {
                string param_in = "Function: " + Function + " PaymExtId: " + PaymExtId + " PaymSubjTp:" + PaymSubjTp + " Amount: " + Amount + " Params: " + Params + " ConnectionTimeout: " + ConnectionTimeout + " Rek:" + Rek + " TotalSum:" + TotalSum;
                GlobalObjectsManager.Logger.Info(param_in);
            }
            string xml_tmpl_resp =
                @"<Response>
<Result>%code%</Result>
<PaymExtId>%id%</PaymExtId>
<Description>%desc%</Description>
</Response>";

            try
            {


                string server_url = Rek;
                if (server_url == null || server_url.Length == 0 || server_url.IndexOf("http") < 0)
                    return GetResponseError(PaymExtId);

                obj = (IDataExchange)Activator.GetObject(
                    typeof(IDataExchange),
                    server_url);
                IDictionary dict = ChannelServices.GetChannelSinkProperties(obj);
                dict["timeout"] = (m_ServerTimeOut / 2);

                ConnectionTimeout *= 1000;
                ConnectionTimeout -= m_ServerTimeOut;

                int req_type = 0;

                if (string.Compare(Function, "check", true) == 0)
                    req_type = 1;
                else
                    if (string.Compare(Function, "payment", true) == 0)
                        req_type = 2;

                long lAmount = long.Parse(Amount);
                long lTotalSum = long.Parse(TotalSum);

                string[] tsp_par = GlobalObjectsManager.ControlTotalSum(int.Parse(PaymSubjTp));
                if (tsp_par != null)
                {
                    string tsp_totalsum = tsp_par[1];

                    int m = ((lAmount % 100) > 0) ? 1 : 0;
                    lTotalSum = (lAmount / 100) + m;

                    GlobalObjectsManager.Logger.Info("FIX TotalSum : " + " lTotalSum * 100: " + (lTotalSum * 100).ToString());


                    if (tsp_par.Length > 2)
                    {
                        GlobalObjectsManager.Logger.Info("lAmount : " + lAmount.ToString() + " lTotalSum * 100: " + (lTotalSum * 100).ToString());

                        if (lAmount <= lTotalSum * 100)
                        {
                            double dbl_reward = 0;
                            double min_reward = 0;

                            if (tsp_par[2].IndexOf('!') > -1)
                            {
                                string[] str_reward = tsp_par[2].Split('!');
                                dbl_reward = double.Parse(str_reward[0].Replace('.', ','));
                                min_reward = double.Parse(str_reward[1]);
                            }
                            else
                                dbl_reward = double.Parse(tsp_par[2].Replace('.', ','));

                            if (dbl_reward > 0 || min_reward > 0)
                            {
                                double dbl_DebtAmount = (double)lAmount;
                                double dbl_sum_reward = dbl_DebtAmount * dbl_reward;
                                dbl_sum_reward = GlobalObjectsManager.Round(dbl_sum_reward, 2);

                                if (min_reward > 0 && min_reward > dbl_sum_reward)
                                    dbl_sum_reward = min_reward;

                                GlobalObjectsManager.Logger.Info("FIX dbl_sum_reward: " + dbl_sum_reward);

                                if (tsp_par.Length > 3)
                                {
                                    if (tsp_par[3] == "1")
                                    {
                                        lTotalSum = lAmount + (long)dbl_sum_reward;
                                    }
                                }
                                else
                                    lAmount =(long) (dbl_DebtAmount - dbl_sum_reward);

                                //GlobalObjectsManager.Logger.Info("FIX lAmount: " + lAmount);
                                //lAmount = (long)(dbl_DebtAmount);

                                if (tsp_par.Length > 4)
                                {
                                    if (tsp_par[3] == "1" && tsp_par[4] == "10")
                                    {
                                        //округлять до 10 рублей в большую сторону
                                        var rub = RoundUp((int)(lTotalSum / 100));
                                        lTotalSum = rub;
                                        GlobalObjectsManager.Logger.Info("FIX after RoundUpTo10Rub lAmount: " + lAmount);
                                    }
                                }
                            }
                        }
                        else
                        {
                            throw new Exception("fatal;суммы несовпадают.");
                        }
                    }

                    Params += ";" + tsp_totalsum + " " + lTotalSum;
                    GlobalObjectsManager.Logger.Info("FIX Params: " + Params);

                }

                int tspCode = int.Parse(PaymSubjTp);
                if (GlobalObjectsManager.DataParameterReplacer.ContainsKey(tspCode))
                {
                    bool anyFix= false;
                    NameValueCollection nvc = GetNameValueCollection(Params);
                    GlobalObjectsManager.ReplaceDataParameter r = GlobalObjectsManager.DataParameterReplacer[tspCode];
                    string pc = nvc[r.ParameterCode.ToString()];
                    if (!string.IsNullOrEmpty(pc))
                    {
                        if (pc.IndexOf(r.OldSimbol, StringComparison.Ordinal) > -1)
                        {
                            anyFix = true;
                            pc = pc.Replace(r.OldSimbol, r.NewSimbol);
                            nvc.Remove(r.ParameterCode.ToString());
                            nvc.Add(r.ParameterCode.ToString(), pc);
                        }
                    }

                    if (anyFix)
                    {
                        Params = string.Empty;
                        foreach (string key in nvc.AllKeys)
                        {
                            if (!string.IsNullOrEmpty(Params))
                                Params += ";";
                            Params += key + " " + nvc[key];
                        }
                    }
                }



                string[] data_pay = new string[4];
                int p = 0;
                
                Params += ";" + ConfigurationManager.AppSettings["DefaultTotalId"] + " " + TotalSum;
                
                data_pay[p++] = PaymExtId;
                data_pay[p++] = lAmount.ToString();
                data_pay[p++] = PaymSubjTp;
                //string Params_update = Params.Insert(0,"%");
                //Params_update = Params_update.Replace(" ", "%");
                data_pay[p++] = Params;
                GlobalObjectsManager.Logger.Info("ALL Params: " + Params);

                //Debug.WriteLine(" SUM {0} ", data_pay[1]);
                string tsp_code = data_pay[(int)eMobile.tsp_code];
                string resp = null;
                int iCurrentState = -1;
                string err = null;

                //for (int i = 0; i < data_pay.Length; i++)
                //  GlobalObjectsManager.Logger.Info("i: " + i + " data: " + data_pay[i]);

                bool ret = function(data_pay, 3, ConnectionTimeout, out resp);
                if (ret)
                {
                    if (resp.IndexOf("REST=") >= 0)
                        iCurrentState = 11;
                    else
                        if (resp.IndexOf("RESULT=1") >= 0)
                            iCurrentState = 1;
                        else
                            if (resp.IndexOf("RESULT=3") >= 0)
                                iCurrentState = 3;
                            else
                                if (resp.IndexOf("RESULT=7") >= 0)
                                    iCurrentState = 7;
                                else
                                {
                                    GlobalObjectsManager.Logger.Debug("UNKNOWN STATE resp is " + resp);
                                }
                }
                else
                {
                    if (resp.IndexOf("ERROR=11") >= 0)
                        iCurrentState = 0;
                    else
                    {
                        if (resp.IndexOf("ERROR=21") >= 0)
                        {
                            iCurrentState = 88;
                        }
                        else
                            if (resp.IndexOf("ERROR=") >= 0)
                            {
                                iCurrentState = 99;
                            }
                            else
                                if (resp.IndexOf("RESULT=45") >= 0)
                                {
                                    iCurrentState = 45;
                                }

                                else
                                {
                                    //if (resp != null)
                                    //  GlobalObjectsManager.Logger.Debug("Status failed resp is " + resp);
                                    //else
                                    //  GlobalObjectsManager.Logger.Debug("Status failed resp is null");
                                }
                    }
                }


                xml_tmpl_resp = xml_tmpl_resp.Replace("%id%", PaymExtId);

                if (iCurrentState == 11)
                {
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "OK");
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", resp);
                    return xml_tmpl_resp;
                }

                if (iCurrentState == -1)
                {
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "Error");
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", "UNKNOWN STATE");
                    return xml_tmpl_resp;
                }

                if (iCurrentState == 99)
                {
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "Error");
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", resp);
                    return xml_tmpl_resp;
                }

                if (iCurrentState == 88)
                {
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "Error");
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", "Общая ошибка системы (CyberPlat) требуется вручную повторить платеж");
                    return xml_tmpl_resp;
                }

                if (iCurrentState == 45)
                {
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "Error");
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", "Закрыт доступ в Киберплате");
                    //GlobalObjectsManager.Logger.Error(PaymExtId + " 45 ERROR CODE IS DONE");
                    return xml_tmpl_resp;
                }

                if ((iCurrentState == 3 || iCurrentState == 7))
                {

                    if (req_type == 1)
                        xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "Error");
                    else
                        xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "OK");
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", "такой платеж уже был");
                    return xml_tmpl_resp;
                }
                else
                    if (req_type == 1 && iCurrentState == 1)
                    {
                        xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "OK");
                        xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", "такая проверка уже была");
                        return xml_tmpl_resp;
                    }

                if (iCurrentState == 0)
                {
                    ret = function(data_pay, 1, ConnectionTimeout, out resp);
                }

                //                Debug.WriteLine("#check done with: {0}", resp);
                if (ret && req_type == 2)
                {
                    ret = function(data_pay, 2, ConnectionTimeout, out resp);
                    //Debug.WriteLine("#payment done with: " + resp);
                }
                xml_tmpl_resp = xml_tmpl_resp.Replace("%id%", PaymExtId);
                if (!ret)
                {
                    err = obj.GetLastError();
                    //Debug.WriteLine("#ERROR FOUND: {0}", err);

                    xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "Error");
                    if (err != null && err.Length > 0)
                        xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", err);
                }
                else
                {
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "OK");
                }

                if (resp != null && resp.Length > 0)
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", resp);
            }
            catch (Exception e)
            {
                GlobalObjectsManager.Logger.Error(e);
                xml_tmpl_resp = xml_tmpl_resp.Replace("%id%", PaymExtId);
                xml_tmpl_resp = xml_tmpl_resp.Replace("%code%", "ERROR");
                if (e.Message.IndexOf("fatal") > -1)
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", "inner error;fatal");
                else
                    xml_tmpl_resp = xml_tmpl_resp.Replace("%desc%", "inner error");
            }
            return xml_tmpl_resp;
        }



    }
}
