using System;
using System.Xml;
using log4net;
using System.Web;
using System.Threading;
using System.Collections.Specialized;


/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{
    private GlobalObjectsManager() { }
    static private ILog _logger = LogManager.GetLogger("Monitor");
    static public readonly string CurrPath = HttpContext.Current.Server.MapPath("~/");
    static private readonly XmlDocument XmlOperators = new XmlDocument();


    static public string ServiceProvider(int tspCode, NameValueCollection payParams)
    {
        string ret = string.Empty;
        XmlNode node = XmlOperators.SelectSingleNode("//*/*/id[@platerra='" + tspCode + "']");
        if (node == null)
        {
            Logger.Info("tsp_code " + tspCode + " not found");
            Logger.Info("tsp_code " + tspCode + " try to find by list...");
            node = XmlOperators.SelectSingleNode("//*/*/id[contains(@platerra,'," + tspCode + ",')]");
            if (node == null)
            {
                throw new Exception("TSP " + tspCode + " not found in dictionary");
            }
            
        }
        ret = "&serviceProviderId=" + node.InnerText + "&phoneNumber=" + payParams[0];
        if (node.ParentNode != null)
        {
            //XmlNode nodeName = node.ParentNode.SelectSingleNode("params/param[@name]");
            //if (nodeName != null && nodeName.Attributes !=null && nodeName.Attributes.Count>0)
            //{
            //    string name = nodeName.Attributes["name"].Value;
            //    string value = "";

            //    if (nodeName.Attributes["platerra"] != null)
            //    {
            //        string kod = nodeName.Attributes["platerra"].Value;
            //        value = payParams[kod];
            //    }
            //    else
            //    {
            //        XmlNode nodeValue = nodeName.SelectSingleNode("elements/item[@platerra='" + tspCode + "']");
            //        if (nodeValue != null)
            //        {
            //            if (nodeValue.Attributes != null && nodeValue.Attributes["value"]!=null) 
            //                value = nodeValue.Attributes["value"].Value;
            //        }
            //        else
            //            value = payParams[1];
            //    }

            //    ret += "&params['"+name+"']="+value;
            //}

            XmlNodeList list = node.ParentNode.SelectNodes("params/param[@name]");
            if (list != null)
                foreach (XmlNode item in list)
                {
                    string name = item.Attributes["name"].Value;
                    string value = "";
                    if (item.Attributes["platerra"] != null)
                    {
                        string kod = item.Attributes["platerra"].Value;
                        value = payParams[kod];
                    }
                    else
                    {
                        XmlNode nodeValue = item.SelectSingleNode("elements/item[@platerra='" + tspCode + "']");
                        if (nodeValue != null)
                        {
                            if (nodeValue.Attributes != null && nodeValue.Attributes["value"] != null)
                                value = nodeValue.Attributes["value"].Value;
                        }
                        else
                            value = payParams[1];
                    }
                    ret += "&params['" + name + "']=" + value;
                }
        }
        return ret;
    }



    /// <summary>
    /// Возвращает объект журнала событий.
    /// 
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
    /// 
    static GlobalObjectsManager()
    {
        log4net.Config.XmlConfigurator.Configure();
        XmlOperators.Load(CurrPath + "PlaterraOperators.xml");

    }

    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    static public void Init()
    {
        Helpers.Net.Net.Init();
    }

}
