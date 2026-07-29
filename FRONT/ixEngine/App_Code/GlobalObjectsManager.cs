using System;
using System.Web;
using System.Collections;
using log4net;
using System.Data.SqlClient;
using System.Data;


/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
/// 
/*
public sealed class GlobalObjectsManager
{
    private GlobalObjectsManager() { }

    static private ILog _logger = LogManager.GetLogger("Monitor");

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
    }




}
*/


public class CsGlobal
{
    // public static string ConnectionString = ConfigurationManager.ConnectionStrings["CsDataModel"].ConnectionString;
    static CsGlobal()
    {
        log4net.Config.XmlConfigurator.Configure();
    }
    static public void Init()
    {
        log4net.Config.XmlConfigurator.Configure();
    }
    //static private ILog _logger = LogManager.GetLogger("application-log");

    public static Logger Logger = new Logger();

}
public class Logger
{
    //private static ILog _logger = LogManager.GetLogger("applcation-log");
    private static ILog _logger = LogManager.GetLogger("Monitor");

    private string GetGuid()
    {
        return "";// HttpContext.Current.Request.GetHashCode().ToString("X").PadLeft(8, '0');

    }
    public void Error(object message)
    {
        message = "[" + GetGuid() + "] - " + message;
        _logger.Error(message);
    }
    public void Error(object message, Exception exception)
    {
        message = "[" + GetGuid() + "] - " + message;
        _logger.Error(message, exception);
    }
    public void Info(object message)
    {
        message = "[" + GetGuid() + "] - " + message;
        _logger.Info(message);
    }
    public void Info(object message, Exception exception)
    {
        message = "[" + GetGuid() + "] - " + message;
        _logger.Info(message, exception);
    }
}