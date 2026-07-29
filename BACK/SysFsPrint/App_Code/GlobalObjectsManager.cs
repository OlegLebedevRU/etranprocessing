using System;
using System.Xml;
using Helpers.Collection;
using log4net;
using System.Web;
using System.Threading;
using System.Collections.Specialized;


/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{
    private GlobalObjectsManager() { }
    static private ILog _logger = LogManager.GetLogger("Monitor");
    static public readonly string CurrPath = HttpContext.Current.Server.MapPath("~/");
    static private readonly NameValueCollection OperCodes = new NameValueCollection();

    static public int GetOperCode(int terminal)
    {
        string opcode = OperCodes[terminal.ToString()];
        return int.Parse(opcode);
    }



    /// <summary>
    /// Возвращает объект журнала событий.
    /// 
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
        OperCodes = Collection.GetNameValueCollection(EtranConfigurationManager.OperCodes, ";", "=");

    }

    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    static public void Init()
    {
        Helpers.Net.Net.Init();
    }

}
