using System;
using System.Text;
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


    static public string ExternalTsp(int tspCode, out int paramsCount)
    {
        string externalTsp = "0";
        paramsCount = 0;
        XmlNode node = XmlOperators.SelectSingleNode("//*/*/*/code[@platerra='" + tspCode + "']");
        if (node == null)
        {
            Logger.Info("tsp_code " + tspCode + " not found default - сотовая");
        }
        else
        {
            externalTsp = node.InnerText;
            if (node.ParentNode != null)
            {
                XmlNodeList list = node.ParentNode.SelectNodes("params/param");
                if (list != null) paramsCount = list.Count;
            }
        }
        return externalTsp;
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
        //CreateReferenceTsp();
        //string tsp = string.Empty;
        //XmlNodeList list = XmlOperators.SelectNodes("//*/*/*/code");
        //var sb = new StringBuilder();
        //foreach (XmlNode item in list)
        //{
        //    tsp += ", " + item.Attributes["platerra"].Value;
        //    sb.AppendLine(item.InnerText + " " + item.ParentNode["name"].InnerText);
        //}

        //System.IO.File.WriteAllText(@"C:\111.txt", sb.ToString());
        //System.IO.File.WriteAllText(@"C:\222.txt", tsp);

    }


    static public void CreateReferenceTsp()
    {
        try
        {

            XmlDocument doc = new XmlDocument();
            XmlNode docNode = doc.CreateXmlDeclaration("1.0", "UTF-8", null);
            doc.AppendChild(docNode);

            XmlNode infoNode = doc.CreateElement("info");
            doc.AppendChild(infoNode);

            var lines = System.IO.File.ReadAllLines(CurrPath + "1.txt");
            foreach (var line in lines)
            {
                var tsp = line.Split('\t');

                XmlNode serviceNode = doc.CreateElement("service");
                XmlNode codeNode = doc.CreateElement("code");
                codeNode.InnerText = tsp[0];
                XmlAttribute plateraAttribute = doc.CreateAttribute("platerra");
                plateraAttribute.Value = "";
                codeNode.Attributes.Append(plateraAttribute);

                XmlNode nameNode = doc.CreateElement("name");
                nameNode.InnerText = tsp[1];

                XmlNode paramsNode = doc.CreateElement("params");
                XmlNode paramNode = doc.CreateElement("param");

                XmlAttribute paramNameAttribute = doc.CreateAttribute("name");
                paramNameAttribute.Value = "1";
                paramNode.Attributes.Append(paramNameAttribute);

                XmlAttribute paramdescriptionAttribute = doc.CreateAttribute("description");
                paramdescriptionAttribute.Value = "Номер счета";
                paramNode.Attributes.Append(paramdescriptionAttribute);


                paramsNode.AppendChild(paramNode);

                serviceNode.AppendChild(codeNode);
                serviceNode.AppendChild(nameNode);
                serviceNode.AppendChild(paramsNode);

                infoNode.AppendChild(serviceNode);

            }
            doc.Save(CurrPath + "out.xml");
        }
        catch (Exception ex)
        {
        }
    }

    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    static public void Init()
    {
        //Helpers.Net.Net.Init();
    }

}
