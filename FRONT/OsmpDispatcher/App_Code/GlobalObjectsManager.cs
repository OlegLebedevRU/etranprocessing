using System;
using System.IO;
using log4net;
using System.Configuration;
using System.Collections.Specialized;
using System.Collections;
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
    static public NameValueCollection TspReplace = EtranLib.Collection.Collection.GetNameValueCollection(ConfigurationManager.AppSettings["TspReplace"], ";", ":");
    static public readonly string curr_path = HttpContext.Current.Server.MapPath("~/");
    static public bool IsRunApp = false;
    static Thread th_reestr;
    static private Hashtable htbl_post_pay = new Hashtable();
    static private readonly string m_ParamsTspCheck = ConfigurationManager.AppSettings["ParamsTspCheck"];


    static public string ParamsTspCheck(int tsp_code, string req_params)
    {
        Logger.Info("ParamsTspCheck IN tsp_code " + tsp_code + " req_params " + req_params);
        if (m_ParamsTspCheck.IndexOf("," + tsp_code + ",") > -1)
        {
            if (req_params.IndexOf(';') == -1)
                if (req_params.Length > 17)
                {
                    Logger.Info("ParamsTspCheck PROCESS");
                    req_params = req_params.Substring(0, req_params.Length - 10) + ";" + req_params.Substring(req_params.Length - 10);
                }
        }
        Logger.Info("ParamsTspCheck OUT tsp_code " + tsp_code + " req_params " + req_params);
        return req_params;
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
            
    static void PayHandler()
    {
        Logger.Info("PayHandler START");

        while (IsRunApp)
        {
            try
            {
                string[] files = Directory.GetFiles(EtranConfigurationManager.FailedPayLog);
                Logger.Info("PayHandler FOUND FILES: " + files.Length);
                foreach (string file in files)
                {
                    Thread.Sleep(3000);
                    Logger.Info("PayHandler FOUND FILE: " + file);
                    
                    FileInfo fi = new FileInfo(file);
                    string filename = fi.Name;
                    int count = 0;
                    if (htbl_post_pay[filename] != null)
                    {
                        count = int.Parse(htbl_post_pay[filename].ToString());
                        htbl_post_pay[filename] = count + 1;
                    }
                    else
                    {
                        htbl_post_pay.Add(filename,count);
                    }

                    Logger.Info("PayHandler: " + filename + " TRIED count: " + count);

                    if (count < 3)
                    {
                        Guid guid = Guid.NewGuid();
                        Logger.Info("PayHandler TRY PROCESS: " + filename + " guid: " + guid);
                        string req = File.ReadAllText(file);
                        EtranDispatcher.Dispatcher.ReqHandler(guid, req);
                        Logger.Info("PayHandler PROCESS DONE: " + filename);
                    }
                    else
                    {
                        Logger.Info("PayHandler LIMIT EXCEEDED TRY MOVE: " + filename);
                        File.Move(file, EtranConfigurationManager.FixPayLog + filename);
                        Logger.Info("PayHandler FILE MOVED TO " + EtranConfigurationManager.FixPayLog + filename);
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Error("PayHandler", ex);
            }

            WaitFor(EtranConfigurationManager.Interval);
        }
        
        Logger.Info("PayHandler DONE");
    }

    static void WaitFor(object time)
    {
        int start = (int)time;
        int timesleep = 10;

        while ((start -= timesleep) > 0)
        {
            if (!IsRunApp)
                return;

            System.Threading.Thread.Sleep(timesleep);
        }
    }

    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    static public void Init()
    {
        log4net.Config.XmlConfigurator.Configure();
        EtranLib.Net.Net.Init();
        IsRunApp = true;
        th_reestr = new Thread(PayHandler);
        th_reestr.IsBackground = true;
        th_reestr.Start();
    }

    static public void UnInit()
    {
        IsRunApp = false;
        Thread.Sleep(100);
        try
        {
            if (th_reestr != null && th_reestr.IsAlive)
            {
                Logger.Info("UnInit: try to abort");
                th_reestr.Abort();
            }
        }
        catch (Exception ex)
        {
            Logger.Error("UnInit, ", ex);
        }
    }

}
