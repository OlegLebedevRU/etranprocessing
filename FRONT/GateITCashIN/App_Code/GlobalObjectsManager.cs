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
        DbConnectionString = GetDBConn(EtranConfigurationManager.DBConnName);

    }

    static public void UnInit()
    {
    }

}
