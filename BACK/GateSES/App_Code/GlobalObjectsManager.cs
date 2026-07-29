using System;
using System.Text;
using log4net;
using System.Web;
using System.Collections.Specialized;


/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{


    static private ILog _logger = LogManager.GetLogger("Monitor");
    static public readonly string DbConnectionString = string.Empty;
    static public readonly string curr_path = HttpContext.Current.Server.MapPath("~/");



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
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    /// 
    static GlobalObjectsManager()
    {
        
        log4net.Config.XmlConfigurator.Configure();
        DbConnectionString = GetDBConn(EtranConfigurationManager.DbConnName);
        Logger.Info("curr_path: " + curr_path);
    }


    static public void Init()
    {
    }

    static public void UnInit()
    {
    }
}
