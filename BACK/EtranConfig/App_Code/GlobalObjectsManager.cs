using System;
using System.Xml;
using System.Threading;
using log4net;
//using EtranLib.Net;
//using EtranLib.Crypto;
//using EtranLib.ScheduleTimer;
//using System.Security.Cryptography.X509Certificates;
/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{
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
    /// 
    static GlobalObjectsManager()
    {
        log4net.Config.XmlConfigurator.Configure();
        EtranLib.Net.Net.Init();

        //Net.Init();
        //Cert = Crypto.GetCertByString(EtranConfigurationManager.Cert);

        //ThreadStart starter = delegate { GateTvingo.Reestr.PostReestr };
        //new Thread(GateTvingo.Reestr.PostReestr).Start();

    }

    static public void Init()
    { 
        //ReqPayment.Load(EtranConfigurationManager.ReqPayment);
    }

}
