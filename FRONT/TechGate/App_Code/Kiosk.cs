// Decompiled with JetBrains decompiler
// Type: TechGate.Kiosk
// Assembly: TechGate, Version=1.0.4724.32104, Culture=neutral, PublicKeyToken=null
// MVID: 7D25CA8F-6EB1-4AE3-AFB7-31AA5758287A
// Assembly location: C:\PlaterraSvn\PROCESSING\FRONT\TechGate\bin\TechGate.dll

using EtranLib.Data;
using System;
using System.Collections;
using System.Collections.Specialized;
using System.Data;

namespace TechGate
{
  public class Kiosk
  {
    public static string CheckModel24(NameValueCollection qparams)
    {
      try
      {
        GlobalObjectsManager.Logger.Info((object) "CheckModel24 START...");
        int num1 = int.Parse(qparams["number"]);
        GlobalObjectsManager.Logger.Info((object) ("CheckModel24 KioskGetModel FOR NUMBER: " + (object) num1));
        int num2 = int.Parse(((Hashtable) new DBManager(EtranConfigurationManager.DBConn).Execute("techgate_kiosk_model", CommandType.StoredProcedure, DBManager.DataReadType.Hashtable, (NameValueCollection) null, (object) num1))[(object) 0].ToString());
        GlobalObjectsManager.Logger.Info((object) string.Concat(new object[4]
        {
          (object) "CheckModel24 MODEL_ID: ",
          (object) num2,
          (object) " FOR NUMBER: ",
          (object) num1
        }));
        return num2 == 24 ? "1" : "0";
      }
      catch (Exception ex)
      {
        GlobalObjectsManager.Logger.Error((object) "При вызове InkassMessage:", ex);
      }
      return "0";
    }

    public static string GetTspList(NameValueCollection qparams)
    {
      try
      {
        GlobalObjectsManager.Logger.Info((object) "GetTspList START...");
        int num = int.Parse(qparams["number"]);
        GlobalObjectsManager.Logger.Info((object) ("GetTspList FOR NUMBER: " + (object) num));
        string str = ((Hashtable) new DBManager(EtranConfigurationManager.DBConn).Execute("TechGate_GetTspList", CommandType.StoredProcedure, DBManager.DataReadType.Hashtable, (NameValueCollection) null, (object) num))[(object) 0].ToString();
        GlobalObjectsManager.Logger.Info((object) string.Concat(new object[4]
        {
          (object) "GetTspList  FOR NUMBER: ",
          (object) num,
          (object) " RECV: ",
          (object) str
        }));
        return str;
      }
      catch (Exception ex)
      {
        GlobalObjectsManager.Logger.Error((object) "При вызове InkassMessage:", ex);
      }
      return "";
    }
  }
}
