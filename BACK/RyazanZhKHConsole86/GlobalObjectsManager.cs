using System;
using log4net;
using System.Runtime.InteropServices;
using System.Web;
using System.Collections.Specialized;


/// <summary>
/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
/// место их хранения. Все методы потокобезопасные.
/// </summary>
public sealed class GlobalObjectsManager
{

    public enum enKVCMethod { InitTerminal, Free, GetInfo, SetMode, ExpCons, FZS, Change, Distribute, Payment };


    [DllImport(@"C:\Projects\ConsoleApplication1\bin\Debug\DLL.dll")]
    static extern int InitBase([MarshalAs(UnmanagedType.AnsiBStr)] string baza, [MarshalAs(UnmanagedType.AnsiBStr)] string sog);

    [DllImport(@"C:\Projects\ConsoleApplication1\bin\Debug\DLL.dll")]
    static extern int InitRegion(int region);
    
    [DllImport(@"C:\Projects\ConsoleApplication1\bin\Debug\DLL.dll")]
    static extern int InitTerminal(int BankCode, int OperCode);

    [DllImport(@"C:\Projects\ConsoleApplication1\bin\Debug\DLL.dll")]
    static extern int Free();

    [DllImport(@"C:\Projects\ConsoleApplication1\bin\Debug\DLL.dll", CallingConvention = CallingConvention.StdCall,CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.AnsiBStr)]
    static extern string GetInfo([MarshalAs(UnmanagedType.AnsiBStr)] string e_street, [MarshalAs(UnmanagedType.AnsiBStr)] string e_house, [MarshalAs(UnmanagedType.AnsiBStr)] string e_corp, [MarshalAs(UnmanagedType.AnsiBStr)] string e_place, [MarshalAs(UnmanagedType.AnsiBStr)] string e_komnata, [MarshalAs(UnmanagedType.AnsiBStr)] string e_contr, out int retcode);

    [DllImport(@"C:\Projects\ConsoleApplication1\bin\Debug\DLL.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.AnsiBStr)]
    static extern string SetMode(int mode, out int retcode);

    [DllImport(@"C:\Projects\ConsoleApplication1\bin\Debug\DLL.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.AnsiBStr)]
    static extern string ExpCons(int value, int service_vid, int service_num, out int retcode);

    [DllImport(@"C:\Projects\ConsoleApplication1\bin\Debug\DLL.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.AnsiBStr)]
    static extern string FZS(int value, int service_vid, int service_num, out int retcode);


    [DllImport(@"C:\Projects\ConsoleApplication1\bin\Debug\DLL.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.AnsiBStr)]
    static extern string Change(int value, int service_type, int service_vid, int service_num, out int retcode);



    [DllImport(@"C:\Projects\ConsoleApplication1\bin\Debug\DLL.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.AnsiBStr)]
    static extern string Distribute(int amount,out int retcode);

    [DllImport(@"C:\Projects\ConsoleApplication1\bin\Debug\DLL.dll")]
    static extern int Payment();


    static private ILog _logger = LogManager.GetLogger("Monitor");
    static public readonly string DbConnectionString = string.Empty;
    static public readonly object Locker = new object();
    public static readonly string curr_path =  System.IO.Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location) + "\\";



 
    static public readonly bool KVC_InitOK = false;
    static public readonly System.Threading.Thread DBUpdater = null;
    static public readonly System.Threading.Thread Reestr = null;
    static private readonly NameValueCollection OperCodes = new NameValueCollection();
    static private readonly NameValueCollection OrgOperCodes = new NameValueCollection();



    static bool apprun = false;
    static public bool AppRun
    {
        get
        {
            return apprun;
        }
    }

    static public int GetOperCode(int terminal)
    {
        string opcode = OperCodes[terminal.ToString()];
        return int.Parse(opcode);
    }
static public int GetOrgOperCode(int orgId)
    {
        string opcode = OrgOperCodes[orgId.ToString()];
        return opcode ==null ? 0 : int.Parse(opcode);
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
    
    static private int _KvcLastError = 0;
    static public int KvcLastError
    {
        get
        {
            return _KvcLastError;
        }
    }

    static public string KVC_Exec(enKVCMethod method, params object[] list)
    {
        string params_list = string.Empty;
        foreach (object obj in list)
            params_list += " " + obj.ToString();

        GlobalObjectsManager.Logger.Info("KVC_Exec method: " + method.ToString() + params_list);

        string ret = string.Empty;
        try
        {
            switch (method)
            {

                case enKVCMethod.InitTerminal:
                    _KvcLastError = InitTerminal((int)list[0], (int)list[1]);
                    ret = _KvcLastError.ToString();
                    break;
                case enKVCMethod.GetInfo:
                    _KvcLastError = 0;
                    ret = GetInfo((string)list[0], (string)list[1], (string)list[2], (string)list[3], (string)list[4], (string)list[5], out _KvcLastError);
                    break;
                case enKVCMethod.Free:
                    _KvcLastError = Free();
                    ret = _KvcLastError.ToString();
                    break;
                case enKVCMethod.SetMode:
                    _KvcLastError = 0;
                    ret = SetMode((int)list[0], out _KvcLastError);
                    break;
                case enKVCMethod.ExpCons:
                    _KvcLastError = 0;
                    ret = ExpCons((int)list[0], (int)list[1], (int)list[2], out _KvcLastError);
                    break;
                case enKVCMethod.FZS:
                    _KvcLastError = 0;
                    ret = FZS((int)list[0], (int)list[1], (int)list[2], out _KvcLastError);
                    break;
                case enKVCMethod.Change:
                    _KvcLastError = 0;
                    ret = Change((int)list[0], (int)list[1], (int)list[2], (int)list[3], out _KvcLastError);
                    break;
                case enKVCMethod.Distribute:
                    _KvcLastError = 0;
                    //for (int i = 0; i < list.Length; i++)
                    //{
                    //    GlobalObjectsManager.Logger.Info("distr List: " + list[i]);
                    //}
                    var amount = (int) list[0];
                    //var amount = 435000;
                    GlobalObjectsManager.Logger.Info("distr Amount: " + amount);
                    ret = Distribute(amount, out _KvcLastError);
                    GlobalObjectsManager.Logger.Info("distr ret: " + ret);
                    
                    ret = _KvcLastError.ToString();
                    break;
                case enKVCMethod.Payment:
                    _KvcLastError = Payment();
                    ret = _KvcLastError.ToString();
                    break;
            }

            if (ret == null)
                ret = string.Empty;
        }
        catch (Exception ex)
        {
            Logger.Error("KVC_Exec", ex);
        }
        Logger.Info("_KvcLastError: " + _KvcLastError + " RET: "+ ret);

        return ret;
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


    /// <summary>
    /// Инициализирует менеджер глобальных объектов.
    /// </summary>
    /// 
    static GlobalObjectsManager()
    {
        
        log4net.Config.XmlConfigurator.Configure();
        OperCodes = Collection.GetNameValueCollection(EtranConfigurationManager.OperCodes, ";", "=");
        if (EtranConfigurationManager.OrgOperCodes!=null)
        OrgOperCodes = Collection.GetNameValueCollection(EtranConfigurationManager.OrgOperCodes, ";", "=");

        DbConnectionString = GetDBConn(EtranConfigurationManager.DBConnName);
        Logger.Info("curr_path: " + curr_path);

        int InitBaseRet = -1000;
        int InitRegionRet = -1000;
        try
        {
            InitBaseRet = InitBase(curr_path + "BAZA", curr_path + "SOG");
            InitRegionRet = InitRegion(EtranConfigurationManager.Region);
            Logger.Info("InitBaseRet: " + InitBaseRet + " InitRegionRet: " + InitRegionRet);
            if (InitBaseRet == 0 && InitRegionRet == 0)
            {
                KVC_InitOK = true;
                // test kvc api 
                //string ChangeRet0;
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.InitTerminal, EtranConfigurationManager.BankCode, 211);
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.GetInfo, "109", "20", "0", "92", "0", "36");
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.SetMode, 1);
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Change, 5, 2, 5, 1);
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Change, 5, 2, 5, 2);
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Change, 5, 2, 5, 3);
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Change, 5, 2, 7, 1);
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Change, 5, 2, 7, 2);
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Change, 5, 2, 7, 3);
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Change, 5, 2, 17, 0);
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Distribute, 10000);
                //ChangeRet0 = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Free, 0);

            }
        }
        catch (Exception ex)
        {
            Logger.Error("GlobalObjectsManager", ex);
        }

        //if (KVC_InitOK)
            apprun = true;

        //DBUpdater = new System.Threading.Thread(clsDB.Thread);
        //DBUpdater.IsBackground = true;
        //DBUpdater.Start();

        //Reestr = new System.Threading.Thread(clsReestr.Thread);
        //Reestr.IsBackground = true;
        //Reestr.Start();

    }


    static public void Init()
    {
    }

    static public void UnInit()
    {
        apprun = false;
    }
}
