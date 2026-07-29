using System.Xml;
using log4net;
using System;
using System.Net;
using System.Security.Cryptography.X509Certificates;
using System.Web;


/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{
    private GlobalObjectsManager() { }
    static private ILog _logger = LogManager.GetLogger("Monitor");
    static public readonly string curr_path = HttpContext.Current.Server.MapPath("~/");
    static public readonly string DbConnectionString = string.Empty;
    public static readonly X509Certificate2 cert;
    public static readonly System.Net.NetworkCredential Credential;
    public static XmlDocument checkResult = new XmlDocument();
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
    /// 
    static GlobalObjectsManager()
    {
        log4net.Config.XmlConfigurator.Configure();
        DbConnectionString = GetDBConn(EtranConfigurationManager.DBConnName);
        Logger.Info("DbConnectionString: " + DbConnectionString);
        cert = new X509Certificate2(curr_path + EtranConfigurationManager.CertName);
        string login = EtranConfigurationManager.Cred.Split(':')[0];
        string pass = EtranConfigurationManager.Cred.Split(':')[1];
        Credential = new NetworkCredential(login, pass);
        checkResult.Load(curr_path + "PaidSystem_Check.xml");
    }

    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    static public void Init()
    {
        EtranLib.Net.Net.Init();
    }

}
