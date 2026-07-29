using System;
using System.Web;
using System.Xml;
using log4net;
using EtranLib.Files;
using System.Collections.Specialized;


/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{
    private GlobalObjectsManager() { }
    static private ILog _logger = LogManager.GetLogger("application-log");
    static public readonly string curr_path = HttpContext.Current.Server.MapPath("~/");
    static private readonly XmlDocument xml_Errors = new XmlDocument();
    static private readonly XmlDocument xml_Providers = new XmlDocument();
    static public string DbConnectionString;

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




    static public string PlaterraTspMigrate(string _from, string _to)
    {
        string ret = "OK";
        try
        {
            XmlDocument from = new XmlDocument();
            from.Load(curr_path + _from);
            XmlDocument to = new XmlDocument();
            to.Load(curr_path + _to);

            XmlNodeList nodes = from.SelectNodes("//*/*/*/*[@tsp!='81000']");
            foreach (XmlNode node in nodes)
            {
                string prv_id = node.Attributes["prv-id"].Value;
                string tsp = node.Attributes["tsp"].Value;
                XmlNode node_to = to.SelectSingleNode("//*/*/*/*[@prv-id='" + prv_id + "']");
                XmlAttribute newAttr = to.CreateAttribute("tsp");
                newAttr.Value = tsp;
                node_to.Attributes.InsertBefore(newAttr, node_to.Attributes["fiscal-name"]);
            }
            to.Save(curr_path + "newtspbase.xml");
        }
        catch (Exception ex)
        {
            Logger.Error("PlaterraTspMigrate", ex);
        }
        return ret;
    }

    static public string GetPrvId(string tsp_code, NameValueCollection nvc)
    {
        string prv_id = string.Empty;
        try
        {
            XmlNodeList nodes = xml_Providers.SelectNodes("//*/*/*/*[@tsp='" + tsp_code + "']");
            if (nodes != null && nodes.Count > 0)
            {
                if (nodes.Count > 1)
                {
                    if (nodes[0].Attributes["paramcode"] != null)
                    {
                        string paramcode = nodes[0].Attributes["paramcode"].Value;
                        string paramval = nvc[paramcode];
                        if (paramval != null)
                        {
                            foreach (XmlNode node in nodes)
                            {
                                if (node.Attributes["paramval"] != null)
                                {
                                    string a_paramval = node.Attributes["paramval"].Value;
                                    if (a_paramval == paramval)
                                    {
                                        prv_id = node.Attributes["prv-id"].Value;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    prv_id = nodes[0].Attributes["prv-id"].Value;
                }
            }
        }
        catch (Exception ex)
        {
            Logger.Error("GetPrvId", ex);
        }
        return prv_id;
    }


    static public string GetError(int errnum)
    {
        string ret = "Ошибка";
        try
        {
            XmlNode node = xml_Errors.SelectSingleNode("//*/*/*/*[@err_id='" + errnum + "']");
            if (node != null)
            {
                ret += "; " + node.Attributes["err_text"].Value;
                if (node.Attributes["fatal"] != null && node.Attributes["fatal"].Value == "1")
                    ret += "; fatal";
            }
        }
        catch (Exception ex)
        {
            Logger.Error("GetError", ex);
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
        xml_Errors.Load(curr_path + "ResultCodes.xml");
        xml_Providers.Load(curr_path + "PlaterraProviders.xml");
        DbConnectionString = GetDBConn(EtranConfigurationManager.DBConnName);

    }

    static public void UnInit()
    {
    }

}
