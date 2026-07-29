using System;
using log4net;
using System.Web;
using EtranLib.Data;
using System.Data;
using System.Collections.Generic;

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
        DbConnectionString = GetDBConn(EtranConfigurationManager.DBConnName);
            //@"Data Source=DESKTOP-9UGK5E7\SQLEXPRESS;Initial Catalog=Organizations;Integrated Security=True;Encrypt=False;TrustServerCertificate=True";
            
    }

    public static bool IsReady;

    static public void Init()
    {
    }
    ///// <summary>
    ///// Инициализирует менеджер глобальных объектов.
    ///// </summary>
    //static public void Init()
    //{
    //    IsReady = false;
    //    try
    //    {
    //        dicMedParams.Clear();

    //        DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
    //        DataSet ds = (DataSet)db.Execute("RyazanMed_GetParamsList", CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null);
    //        int count = ds.Tables[0].Rows.Count;
    //        Logger.Info("count =" + count);
    //        if (count > 0)
    //        {
    //            for (int i = 0; i < ds.Tables[0].Rows.Count; i++)
    //            {
    //                int id = (int)ds.Tables[0].Rows[i][0];
    //                string name  = ds.Tables[0].Rows[i][1].ToString();
    //                dicMedParams.Add(id, name);
    //            }
    //        }

    //        ds = (DataSet)db.Execute("RyazanMed_GetServicesParams", CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null);
    //        count = ds.Tables[0].Rows.Count;
    //        Logger.Info("count =" + count);
    //        if (count > 0)
    //        {
    //            for (int i = 0; i < ds.Tables[0].Rows.Count; i++)
    //            {
    //                int idSerice = (int)ds.Tables[0].Rows[i][0];
    //                int idParam = (int)ds.Tables[0].Rows[i][1];
    //                if (!dicMedParamsIds.ContainsKey(idSerice))
    //                {
    //                    List<int> l = new List<int>();
    //                    l.Add(idParam);
    //                    dicMedParamsIds.Add(idSerice, l);
    //                }
    //                else
    //                {
    //                    dicMedParamsIds[idSerice].Add(idParam);
    //                }
    //            }
    //        }

    //        ds = (DataSet)db.Execute("RyazanMed_GetServices", CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null);
    //        count = ds.Tables[0].Rows.Count;
    //        Logger.Info("count =" + count);
    //        if (count > 0)
    //        {
    //            for (int i = 0; i < ds.Tables[0].Rows.Count; i++)
    //            {
    //                MedService ms = new MedService();
    //                ms.Id = (int)ds.Tables[0].Rows[i][0];
    //                ms.ServiceName= ds.Tables[0].Rows[i][1].ToString();
    //                ms.KBK = ds.Tables[0].Rows[i][2].ToString();
    //                ms.NDS = (int)ds.Tables[0].Rows[i][3];
    //                if (dicMedParamsIds.ContainsKey(ms.Id))
    //                {
    //                    for (int j = 0; j < dicMedParamsIds[ms.Id].Count; j++)
    //                    {
    //                        MedServiceParam msp = new MedServiceParam();
    //                        msp.Id = dicMedParamsIds[ms.Id][j];
    //                        msp.ParamName = dicMedParams[msp.Id];
    //                        ms.Params.Add(msp);
    //                    }
    //                }
    //                dicMedSerices.Add(ms);
    //            }
    //        }
    //        IsReady = true;
    //    }
    //    catch (Exception ex)
    //    {
    //        Logger.Error(ex);
    //    }

    //}

    //public static Dictionary<int, string> dicMedParams = new Dictionary<int, string>();
    //public static Dictionary<int, List<int>> dicMedParamsIds = new Dictionary<int, List<int>>();

    ////[XmlArray("Services")]
    //public static List<MedService> dicMedSerices = new List<MedService>();


    //public class MedServiceParam
    //{
    //    public int Id;
    //    public string ParamName;
    //}

    //public class MedService
    //{
    //    public int Id;
    //    public string ServiceName;
    //    public string KBK;
    //    public int NDS;
    //    public List<MedServiceParam> Params;

    //    public MedService()
    //    {
    //        Params = new List<MedServiceParam>();
    //    }
    //}
}
