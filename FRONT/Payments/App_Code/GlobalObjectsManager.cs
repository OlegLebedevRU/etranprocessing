using System;
using System.Collections.Generic;
using System.Data;
using System.Globalization;
using System.Text.RegularExpressions;
using EtranLib.Data;
using log4net;
using System.Web;
using System.Threading;



public struct Tuple<T1, T2>
{
    public readonly T1 Item1;
    public readonly T2 Item2;
    public Tuple(T1 item1, T2 item2) { Item1 = item1; Item2 = item2; }
}

public static class Tuple
{ // for type-inference goodness.
    public static Tuple<T1, T2> Create<T1, T2>(T1 item1, T2 item2)
    {
        return new Tuple<T1, T2>(item1, item2);
    }
}


/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{
    private GlobalObjectsManager() { }
    static private ILog _logger = LogManager.GetLogger("Monitor");
    static public readonly string CurrPath = HttpContext.Current.Server.MapPath("~/");
    static public readonly string DbConnectionString = string.Empty;

    static public readonly Dictionary<int, string> TspCodes = new Dictionary<int, string>();
    static public readonly Dictionary<int, Dictionary<int, string>> TspParams = new Dictionary<int, Dictionary<int, string>>();


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


    static List<int> GetRange(int start, int done)
    {
        List<int> range = new List<int>();
        while (start <= done)
            range.Add(start++);
        return range;

    }

    private static string IntToStringConverter(int n)
    {
        return n.ToString(CultureInfo.InvariantCulture);
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
