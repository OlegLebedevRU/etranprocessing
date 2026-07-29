using System;
using System.Reflection;
using log4net;
using System.Net;
using System.Web;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Configuration;
using System.IO;
using System.Text;
using System.Xml;
using System.Xml.Serialization;

public class XmlService
{

    public static Byte[] StringToByteArray(String byteString)
    {
        var encoding = Encoding.UTF8;
        var byteArray = encoding.GetBytes(byteString);
        return byteArray;
    }

    public static T DeSerialize<T>(string xml)
        {
            var serializer = new XmlSerializer(typeof(T));
            var memoryStream = new MemoryStream(StringToByteArray(xml));
            return (T)serializer.Deserialize(memoryStream);
        }


    public static string Serialize(object file)
    {
        string resultString;

        var ns = new XmlSerializerNamespaces();
        ns.Add("", "");

        var xmlWriterSettings = new XmlWriterSettings
        {
            Indent = true,
            OmitXmlDeclaration = false,
            Encoding = Encoding.UTF8
        };

        using (var memoryStream = new MemoryStream())
        using (var xmlWriter = XmlWriter.Create(memoryStream, xmlWriterSettings))
        {
            var x = new XmlSerializer(file.GetType());
            x.Serialize(xmlWriter, file, ns);
            memoryStream.Position = 0; // rewind the stream before reading back.
            using (var sr = new StreamReader(memoryStream, Encoding.UTF8))
            {
                resultString = sr.ReadToEnd();
            } // note memory stream disposed by StreamReaders Dispose()
        }
        return resultString;
    }
}

public class TspExtraParams
{
    public TspExtraParams()
    {
        tsp = new List<Tsp>();
    }

    [XmlArray]
    [XmlArrayItem("tsp")]
    public List<Tsp> tsp { get; set; }

}
public class Tsp
{
    public Tsp()
    {
        Id = string.Empty;
        Terminals = new List<Terminal>();
    }

    [XmlAttribute]
    public string Id { get; set; }

    [XmlArray]
    [XmlArrayItem("terminal")]
    public List<Terminal> Terminals { get; set; }
}

public class Item
{
    [XmlAttribute]
    public string Id { get; set; }

    [XmlAttribute]
    public string Name { get; set; }

    [XmlText]
    public string Value { get; set; }
}

public class Terminal
{
    [XmlAttribute]
    public string Id { get; set; }

    [XmlArray("add")]
    [XmlArrayItem("item")]
    public List<Item> Items { get; set; }
}


/// <summary>
/// Summary description for GlobalObjectsManager.
/// </summary>
/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами приложения. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{

    public class ReplaceDataParameter
    {
        public int TspCode { get; set; }

        public int ParameterCode { get; set; }

        public string OldSimbol { get; set; }

        public string NewSimbol { get; set; }

    }


    static public readonly string curr_path = HttpContext.Current.Server.MapPath("~/");


    static public readonly Dictionary<int, ReplaceDataParameter> DataParameterReplacer = new Dictionary<int, ReplaceDataParameter>();
    static private readonly ILog _logger = LogManager.GetLogger("Monitor");
    static private readonly Hashtable TspTotalSum = new Hashtable();
    static private TspExtraParams _tspExtraParams = new TspExtraParams();

    public static NameValueCollection CheckTspExtraParams(string termNum, string paymSubjTp)//, NameValueCollection payParams)
    {
        NameValueCollection nvc = new NameValueCollection();
        try
        {
            foreach (var tsp in _tspExtraParams.tsp)
            {
                if (tsp.Id == paymSubjTp)
                {
                    foreach (var term in tsp.Terminals)
                    {
                        if (term.Id == termNum || term.Id.IndexOf("," + termNum + ",", StringComparison.Ordinal) > -1)
                        {
                            foreach (var item in term.Items)
                            {
                                nvc.Add(item.Id, item.Value);
                            }
                        }
                    }
                }
            }
        }
        catch (Exception ex)
        {
            Logger.Error(ex);
        }
        return nvc;
    }

    static public string Check_NoDopKomGate(string PaymSubjTp)
    {
        string PlaterraNoDopKomTsp = ConfigurationManager.AppSettings["PlaterraNoDopKomTsp"];
        if (PlaterraNoDopKomTsp != null && PlaterraNoDopKomTsp.Length > 0)
            if (PlaterraNoDopKomTsp.IndexOf("," + PaymSubjTp + ",") > -1)
                return ConfigurationManager.AppSettings["PlaterraNoDopKomUrl"];

        return null;
    }

    static public string[] ControlTotalSum(int PaymSubjTp)
    {

        string param = (string)TspTotalSum[PaymSubjTp];
        if (param != null)
        {
            return param.Split(' ');
        }
        return null;
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

    public static double Round(double value, int digits)
    {
        double scale = Math.Pow(10.0, digits);
        double round = Math.Floor(Math.Abs(value) * scale + 0.5);
        return (Math.Sign(value) * round / scale);
    }

    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    static public void Init()
    {
        log4net.Config.XmlConfigurator.Configure();
        string[] s_TspTotalSum = ConfigurationManager.AppSettings["TspTotalSum"].Split(';');
        for (int i = 0; i < s_TspTotalSum.Length; i++)
        {
            string[] sp = s_TspTotalSum[i].Split(' ');
            TspTotalSum.Add(int.Parse(sp[0]), s_TspTotalSum[i]);
        }
        try
        {
            string[] tspList = ConfigurationManager.AppSettings["ReplaceDataParameter"].Split(new string[] {";"}, StringSplitOptions.None);
            foreach (var s in tspList)
            {
                var n = s.Split(new string[] { ":" }, StringSplitOptions.None);

                int tspCode = int.Parse(n[0]);
                int tspPar = int.Parse(n[1]);
                string oldVal = n[2];
                string newVal = n[3];
                DataParameterReplacer.Add(tspCode , new ReplaceDataParameter() {TspCode = tspCode, ParameterCode = tspPar, OldSimbol = oldVal, NewSimbol = newVal});
            }
        }
        catch (Exception ex)
        {
            Logger.Error(ex);
        }
        try
        {
            string fn = ConfigurationManager.AppSettings["TspExtraParams"];
            if (File.Exists(curr_path + fn))
            {
                XmlDocument doc = new XmlDocument();
                doc.Load(curr_path + fn);
                _tspExtraParams = XmlService.DeSerialize<TspExtraParams>(doc.OuterXml);

                int y = 0;
            }
        }
        catch (Exception ex)
        {
            Logger.Error(ex);
        }
    }
}
