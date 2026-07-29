using System;
using log4net;
using System.Web;
using System.Threading;

/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{
    private GlobalObjectsManager() { }
    static private ILog _logger = LogManager.GetLogger("Monitor");
    static public readonly string curr_path = HttpContext.Current.Server.MapPath("~/");
    static public bool IsRunApp = false;

    static public readonly string DbConnectionString = string.Empty;
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

    //static Thread th_reestr;

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
        //EtranLib.Net.Net.Init();
        IsRunApp = true;
        //th_reestr = new Thread(Reestr.MainCycle);
        //th_reestr.Start();

    }

    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    static public void Init()
    {

    }

    static public void UnInit()
    {
        IsRunApp = false;
        //Thread.Sleep(100);
        //try
        //{
        //    if (th_reestr != null && th_reestr.IsAlive)
        //    {
        //        Logger.Info("UnInit: try to abort");
        //        th_reestr.Abort();
        //    }
        //}
        //catch(Exception ex)
        //{
        //    Logger.Error("UnInit, ", ex);
        //}
    }
}
