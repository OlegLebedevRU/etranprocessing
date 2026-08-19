using System;
using System.Web;
using System.Configuration;
using System.Collections;

namespace EtranDispatcher
{
    /// <summary>
    /// �������� ������������ ����������.
    /// </summary>
    public sealed class EtranConfigurationManager
    {
        static private readonly Hashtable InUtf8Table = new Hashtable();
        static EtranConfigurationManager()
        {
            try
            {
                var t = ConfigurationManager.AppSettings["InUtf8"];
                if (!string.IsNullOrEmpty(t))
                {
                    var coll = t.Split(';');
                    foreach (var i in coll)
                    {
                        if (i.Contains("-"))
                        {
                            var a = i.Split('-');
                            var start = int.Parse(a[0]);
                            var end = int.Parse(a[1]);
                            for (int n = start; n <= end; n++)
                            {
                                InUtf8Table.Add(n, 0);
                            }
                        }
                        else
                        {
                            InUtf8Table.Add(int.Parse(i), 0);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error(ex);
            }
            finally
            {
                GlobalObjectsManager.Logger.Info("ALL InUtf8 KEYS: ");
                foreach (DictionaryEntry key in InUtf8Table)
                {
                    GlobalObjectsManager.Logger.Info(key.Key);
                }
            }
        }
        static public bool IsInUtf8(int tspnum)
        {
            return InUtf8Table.ContainsKey(tspnum);
        }

        static public bool UTF8(string tsp_code)
        {
            //GlobalObjectsManager.Logger.Info("UTF8 " + ConfigurationManager.AppSettings["RetUtf8"] + " START FOR tsp_code: " + tsp_code);
            if (ConfigurationManager.AppSettings["RetUtf8"] != null)
                if (ConfigurationManager.AppSettings["RetUtf8"].IndexOf("," + tsp_code + ",") > -1)
                    return true;
            return false;
        }


        static public string InComeEncodingName
        {
            get
            {
                return ConfigurationManager.AppSettings["InComeEncodingName"].ToString();
            }
        }
        static public string AddDefaultParams
        {
            get
            {
                return ConfigurationManager.AppSettings["AddDefaultParams"].ToString();
            }
        }
        static public string SignKey
        {
            get
            {
                return ConfigurationManager.AppSettings["SignKey"].ToString();
            }
        }

        static public string EtranConfig
        {
            get
            {
                return ConfigurationManager.AppSettings["EtranConfig"];
            }
        }

        static public string DBConnNamePayments
        {
            get
            {
                return ConfigurationManager.AppSettings["DBConnNamePayments"];
            }
        }

        static public string MessageProcessor
        {
            get
            {
                return ConfigurationManager.AppSettings["MessageProcessor"];
            }
        }

        static public string TestMessageProcessor
        {
            get
            {
                return ConfigurationManager.AppSettings["TestMessageProcessor"];
            }
        }

        static public bool ISTestedSerialNumber(string SerialNumber)
        {
            return (ConfigurationManager.AppSettings["SerialNumber"].IndexOf(SerialNumber) > -1) ? true : false;
        }

    }
}
