using System;
using log4net;
using CERTCLIENTLib;
using CERTADMINLib;
using System.Security.Cryptography.X509Certificates;




/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{
    static private ILog _logger = LogManager.GetLogger("Monitor");
    static public readonly string CASerial = string.Empty;
    static public string DbConnectionString = string.Empty;
    static private string lastProject = string.Empty;

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
            ret = (new System.Net.WebClient()).DownloadString(EtranConfigurationManager.EtranConfig + "?function=dbconn&dbname=" + name);
        }
        catch (Exception ex)
        {
            Logger.Error("GetDBConn", ex);
        }
        return ret;
    }


    public static bool UpdateDbConnectionStringFromProject(string project)
    {
        try
        {
            if (string.IsNullOrEmpty(project))
                project = "Service";

            if (lastProject.ToLower() != project.ToLower())
            {
                DbConnectionString = string.Empty;
                DbConnectionString = GetDBConn(project);
                lastProject = project;
            }
            return true;
        }
        catch (Exception ex)
        {
            Logger.Error("GetDBConn", ex);
        }
        return false;
    }

    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    /// 
    static GlobalObjectsManager()
    {
        log4net.Config.XmlConfigurator.Configure();
        CASerial = CAInfoInit();
        Logger.Info("CASerial: " + CASerial);
        if(!EtranConfigurationManager.IsProxyCA)
            DbConnectionString = GetDBConn(EtranConfigurationManager.DBConnName);
        lastProject = EtranConfigurationManager.DBConnName;
    }


    static string CAInfoInit()
    {
        int CR_PROP_CASIGCERT = 12;
        X509Certificate2 objCACert = new X509Certificate2();
        CCertConfigClass certConfig = new CCertConfigClass();
        string config = certConfig.GetConfig(0);
        CCertAdminClass certAdmin = new CCertAdminClass();
        object o_retca = certAdmin.GetCAProperty(config, CR_PROP_CASIGCERT, 0, 3, 0);
        string retca = o_retca.ToString();
        objCACert.Import(System.Text.Encoding.Default.GetBytes(retca));
        return objCACert.SerialNumber;
    }

    static public void Init()
    {
        EtranLib.Net.Net.Init();
    }
}
