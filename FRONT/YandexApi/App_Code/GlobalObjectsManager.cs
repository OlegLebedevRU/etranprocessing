using System;
using log4net;
using System.Web;
using System.Threading;
using EtranLib.Data;
using System.Data;

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


    public static DataSet YandexWallet(int org_id, string function, string val)
    {
        try
        {
            var spName = "YandexWallet_Get";
            switch (function)
            {
                case "info":
                    break;
                case "activate":
                    spName = "YandexWallet_ActivateBegin";
                    break;
                case "finish":
                    spName = "YandexWallet_ActivateDone";
                    break;

            }
            DBManager db = new DBManager(DbConnectionString);
            return (DataSet)db.Execute(spName, CommandType.StoredProcedure, DBManager.DataReadType.DataSet,null, org_id, val);
        }
        catch (Exception exc)
        {
            Logger.Error(exc);
        }
        return null;
    }

    static public string GetDbConn(string name)
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
        DbConnectionString = GetDbConn(EtranConfigurationManager.DbConnName);
        Logger.Info("DbConnectionString: " + DbConnectionString);

    }

    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    static public void Init()
    {
    }

    static public void UnInit()
    {
    }
}
