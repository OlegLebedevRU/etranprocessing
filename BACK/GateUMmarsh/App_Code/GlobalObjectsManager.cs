using System;
using log4net;



/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{
    static private ILog _logger = LogManager.GetLogger("Monitor");
    //static public readonly string DbConnectionString = string.Empty;

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
            Logger.Info("GetDBConn start name " + name);
            string uri = EtranConfigurationManager.EtranConfig + "?function=dbconn&dbname=" + name;
            Logger.Info("GetDBConn start uri " + uri);

            ret = (new System.Net.WebClient()).DownloadString(uri);

            Logger.Info("GetDBConn ret " + ret);
        }
        catch (Exception ex)
        {
            Logger.Error("GetDBConn", ex);
        }
        return ret;
    }

    private static string _dbConnectionString = "";
    public static string DbConnectionString
    {
        get
        {
            if(string.IsNullOrEmpty( _dbConnectionString))
            {
                _dbConnectionString = GetDBConn(EtranConfigurationManager.DBConnName);
                Logger.Info("Init done DbConnectionString " + DbConnectionString);
            }
            return _dbConnectionString;
        }
    }

    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    /// 
    static GlobalObjectsManager()
    {
        log4net.Config.XmlConfigurator.Configure();
        Logger.Info("Init start...");
        _dbConnectionString = GetDBConn(EtranConfigurationManager.DBConnName);
        Logger.Info("Init done DbConnectionString " + DbConnectionString);
    }


    static public void Init()
    {
    }
}
