using System;
using log4net;
using System.Web;
using System.Threading;
using System.Collections.Specialized;
using System.Xml;


/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{
    private GlobalObjectsManager() { }
    static private ILog _logger = LogManager.GetLogger("Monitor");
    static public readonly string curr_path = HttpContext.Current.Server.MapPath("~/");
    static public readonly string LOG = curr_path + "/LOG/";
    static public string DBConn;
    static private readonly XmlDocument xml_Operators = new XmlDocument();


    static public string GetDBConn(string name)
    {
        string ret = string.Empty;
        try
        {
            ret = (new System.Net.WebClient()).DownloadString(EtranConfigurationManager.EtranConfig + "?function=dbconn&dbname=" + name);
        }
        catch (Exception ex)
        {
            Logger.Error("GetDBConn", ex);
        }
        return ret;
    }


    static public string PayParamsString(string tsp_code, NameValueCollection pay_params)
    {
        string ret = string.Empty;
        XmlNode node = xml_Operators.SelectSingleNode("//*/*/*/*[@platerra='" + tsp_code + "']");
        if (node == null)
        {
	       Logger.Info("tsp_code "+tsp_code+" not found");
		Logger.Info("tsp_code "+tsp_code+" try to find by list...");
               node = xml_Operators.SelectSingleNode("//*/*/*/Code[contains(@platerra,'," + tsp_code + ",')]");
		if (node == null)
		{
	 	    throw new Exception("TSP " + tsp_code + " not found in dictionary");
		}
        }
        ret = "OCode=" + int.Parse(node.InnerText);
        XmlNodeList list_params = node.ParentNode.SelectNodes("*/Item");
        for (int i = 0; i < list_params.Count; i++)
        {
            int code = int.Parse(list_params[i]["Order"].InnerText);

            if (tsp_code == "609")
            {
                string pay_param = pay_params[code.ToString()];
                ret += "&Code" + code + "=" + pay_param;

                //if (i==0)
                //    ret += "&Code" + code + "=" + pay_params["1"];
                //else
                //    ret += "&Code" + code + "=" + pay_params["2"];
            }
            else
            {
                //string pay_param = pay_params[code.ToString()];
                string pay_param = pay_params[i];

                ret += "&Code" + code + "=" + pay_param;
            }
        }
        return ret;
    }


    /// <summary>
    /// Возвращает объект журнала событий.
    /// </summary>
    static public ILog Logger
    {
        get
        {
            return _logger;
        }
    }


    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    static public void Init()
    {
        log4net.Config.XmlConfigurator.Configure();
        EtranLib.Net.Net.Init();
        DBConn = GetDBConn(EtranConfigurationManager.DBConnName);
        xml_Operators.Load(curr_path + "PlaterraOperators.xml");
    }

    static public void UnInit()
    {
    }

}
