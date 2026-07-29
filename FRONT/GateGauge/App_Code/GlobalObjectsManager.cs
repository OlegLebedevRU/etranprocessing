// Decompiled with JetBrains decompiler
// Type: GlobalObjectsManager
// Assembly: GateGauge, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null
// MVID: 5E29B6EF-F709-4D1D-AAF0-FCC69378AC17
// Assembly location: C:\Новая папка\20160909\front\GateGauge\bin\GateGauge.dll

using log4net;
using log4net.Config;
using System;
using System.Net;

public sealed class GlobalObjectsManager
{
    private static ILog _logger = LogManager.GetLogger("Monitor");
    private static string DBConn = "";

    public static string GetDBConn()
    {
        if (string.IsNullOrEmpty(DBConn))
            DBConn = GetDBConn(EtranConfigurationManager.DBConnName);
        return DBConn;
    }
    public static ILog Logger
    {
        get
        {
            return GlobalObjectsManager._logger;
        }
    }

    private GlobalObjectsManager()
    {
    }

    public static string GetDBConn(string name)
    {
        string str = string.Empty;
        try
        {
            str = new WebClient().DownloadString(EtranConfigurationManager.EtranConfig + "?function=dbconn&dbname=" + name);
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error((object)"GetDBConn", ex);
        }
        return str;
    }

    public static void Init()
    {
        XmlConfigurator.Configure();
    }
}
