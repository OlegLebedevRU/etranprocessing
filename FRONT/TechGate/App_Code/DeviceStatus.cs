// Decompiled with JetBrains decompiler
// Type: TechGate.DeviceStatus
// Assembly: TechGate, Version=1.0.4724.32104, Culture=neutral, PublicKeyToken=null
// MVID: 7D25CA8F-6EB1-4AE3-AFB7-31AA5758287A
// Assembly location: C:\PlaterraSvn\PROCESSING\FRONT\TechGate\bin\TechGate.dll

using EtranLib.Data;
using System.Collections.Specialized;
using System.Data;
using System.Globalization;
using System.Web;

namespace TechGate
{
  public class DeviceStatus
  {
    public static void ProcessMessage(NameValueCollection _qparams, ref HttpContext Context)
    {
      //string serialNumber = Context.Request.ClientCertificate.SerialNumber;
//string serialNumber = ClientCertHelper.GetSerialNumber(Context);
//      string str = int.Parse(serialNumber.Remove(0, serialNumber.Length - 11).Replace("-", ""), NumberStyles.HexNumber).ToString();
string str = ClientCertHelper.GetSerialNumber(Context);
      GlobalObjectsManager.Logger.Info((object) ("serial_number:" + str));
      new DBManager(EtranConfigurationManager.DBConn).Execute("DeviceLog_Put", CommandType.StoredProcedure, DBManager.DataReadType.ExecuteNonQuery, new NameValueCollection()
      {
        {
          "serial_number",
          str
        },
        {
          "deviceid",
          _qparams["devicetype"]
        },
        {
          "status",
          _qparams["data"]
        }
      });
    }
  }
}
