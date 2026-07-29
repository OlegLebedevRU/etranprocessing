using EtranLib.Data;
using log4net;
using System;
using System.Collections.Specialized;
using System.Data;
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
    static public readonly string _clientID = string.Empty;
    static public readonly string _instanceName = string.Empty;
    static public readonly string _clientSecret = string.Empty;

    public static NameValueCollection GetYandexWalletInfo()
    {
        try
        {
            string spName = "select * from YandexWalletSettings";
            DBManager db = new DBManager(DbConnectionString);
            return (NameValueCollection)db.Execute(spName, CommandType.Text, DBManager.DataReadType.NameValueCollection, null);
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
        var nvc = GetYandexWalletInfo();
        _clientID = nvc["ClientId"];
        _instanceName = nvc["InstanceName"];
        _clientSecret = nvc["ClientSecret"];
    }

    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    static public void Init()
    {
    }

}
