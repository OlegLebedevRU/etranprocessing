using System;
using System.Web;
using System.Xml;
using log4net;
using EtranLib.Files;
using System.Collections;
using System.Configuration;



/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{
    private GlobalObjectsManager() { }
    static private ILog _logger = LogManager.GetLogger("application-log");
    static public readonly string curr_path = HttpContext.Current.Server.MapPath("~/");
    static private readonly XmlDocument xml_Errors = new XmlDocument();
    static public string PaymentDbConnectionString;
    static private readonly Hashtable TspAmountFix = new Hashtable();

    static public string TryTspAmountFix(string PaymSubjTp, string Amount)
    {
        Logger.Info("TryTspAmountFix TRY PaymSubjTp " + PaymSubjTp + " Amount " + Amount);
        string ReturnAmount = Amount;
        try
        {
            string sPercent = (string)TspAmountFix[int.Parse(PaymSubjTp)];
            if (sPercent != null)
            {
                long lAmount = long.Parse(Amount);
                double dbl_reward = double.Parse(sPercent.Replace('.', ','));
                if (dbl_reward > 0)
                {
                    double dbl_DebtAmount = (double)lAmount;
                    double dbl_sum_reward = dbl_DebtAmount * dbl_reward;
                    dbl_sum_reward = GlobalObjectsManager.Round(dbl_sum_reward, 2);
                    dbl_DebtAmount -= dbl_sum_reward;
                    ReturnAmount = ((long)(dbl_DebtAmount)).ToString();
                    Logger.Info("TryTspAmountFix OK ReturnAmount " + ReturnAmount);
                }
            }
        }
        catch (Exception ex)
        {
            Logger.Error("TryTspAmountFix", ex);
        }
        return ReturnAmount;
    }

    public static double Round(double value, int digits)
    {
        double scale = Math.Pow(10.0, digits);
        double round = Math.Floor(Math.Abs(value) * scale + 0.5);
        return (Math.Sign(value) * round / scale);
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


    static public string GetError(int errnum)
    {
        string ret = string.Empty;
        try
        {
            XmlNode node = xml_Errors.SelectSingleNode("//*/*/*[@id='" + errnum + "']");
            if (node != null)
            {
                string fatal = string.Empty;
                if (node.Attributes["fatal"] != null)
                    fatal = ";fatal";

                string err_msg = node.InnerText.Replace('\n', ' ');

                err_msg = err_msg.TrimStart(' ');
                err_msg = err_msg.TrimEnd(' ');

                ret += err_msg + fatal;
            }
        }
        catch (Exception ex)
        {
            Logger.Error("GetError", ex);
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
    static public void Init()
    {
        log4net.Config.XmlConfigurator.Configure();
        EtranLib.Net.Net.Init();
        xml_Errors.Load(curr_path + "ResultCodes.xml");
        PaymentDbConnectionString = GetDBConn(EtranConfigurationManager.DBConnNamePayments);

        string[] s_TspAmountFix = ConfigurationManager.AppSettings["TspAmountFix"].Split(';');
        for (int i = 0; i < s_TspAmountFix.Length; i++)
        {
            string[] sp = s_TspAmountFix[i].Split(' ');
            TspAmountFix.Add(int.Parse(sp[0]), sp[1]);
        }
    }

    static public void UnInit()
    {
    }

}
