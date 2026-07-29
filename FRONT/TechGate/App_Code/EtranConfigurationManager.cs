// Decompiled with JetBrains decompiler
// Type: EtranConfigurationManager
// Assembly: TechGate, Version=1.0.4724.32104, Culture=neutral, PublicKeyToken=null
// MVID: 7D25CA8F-6EB1-4AE3-AFB7-31AA5758287A
// Assembly location: C:\PlaterraSvn\PROCESSING\FRONT\TechGate\bin\TechGate.dll

using System.Configuration;

public sealed class EtranConfigurationManager
{
  public static string EtranConfig
  {
    get
    {
      return ConfigurationManager.AppSettings["EtranConfig"];
    }
  }

  public static string DBConnName
  {
    get
    {
      return ConfigurationManager.AppSettings["DBConnNameService"];
    }
  }

  public static string DBConn
  {
    get
    {
      return GlobalObjectsManager.DBConn;
    }
  }
}
