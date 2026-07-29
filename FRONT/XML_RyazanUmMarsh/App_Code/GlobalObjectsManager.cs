using System;
using System.Collections.Generic;
using System.Data;
using System.Globalization;
using System.Text.RegularExpressions;
using EtranLib.Data;
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
    static public readonly string CurrPath = HttpContext.Current.Server.MapPath("~/");
    static private string _dbConnectionString = string.Empty;

    //static public readonly Dictionary<int, string> TspCodes = new Dictionary<int, string>();
    //static public readonly Dictionary<int, Dictionary<int, string>> TspParams = new Dictionary<int, Dictionary<int, string>>();


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

    public static string DbConnectionString
    {
        get
        {
            if (string.IsNullOrEmpty(_dbConnectionString))
                _dbConnectionString = GetDbConn(EtranConfigurationManager.DbConnName);
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
        _dbConnectionString = GetDbConn(EtranConfigurationManager.DbConnName);
        Logger.Info("DbConnectionString: " + _dbConnectionString);
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
