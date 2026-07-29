// Decompiled with JetBrains decompiler
// Type: GlobalObjectsManager
// Assembly: TechGate, Version=1.0.4724.32104, Culture=neutral, PublicKeyToken=null
// MVID: 7D25CA8F-6EB1-4AE3-AFB7-31AA5758287A
// Assembly location: C:\PlaterraSvn\PROCESSING\FRONT\TechGate\bin\TechGate.dll

using log4net;
using log4net.Config;
using System;
using System.Net;

public sealed class GlobalObjectsManager
{
  private static ILog _logger = LogManager.GetLogger("Monitor");
  private static string dBConn ="";
    public static string DBConn
    {
        get
        {
            if(string.IsNullOrEmpty(dBConn))
            {
                dBConn = GetDBConn(EtranConfigurationManager.DBConnName);
            }
            return dBConn;
        }
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

  private static string GetDBConn(string name)
  {
    string str = string.Empty;
    try
    {
      str = new WebClient().DownloadString(EtranConfigurationManager.EtranConfig + "?function=dbconn&dbname=" + name);
    }
    catch (Exception ex)
    {
      GlobalObjectsManager.Logger.Error((object) "GetDBConn", ex);
    }
    return str;
  }

  public static void Init()
  {
    XmlConfigurator.Configure();
        Logger.Info("Init...");
  }
}
