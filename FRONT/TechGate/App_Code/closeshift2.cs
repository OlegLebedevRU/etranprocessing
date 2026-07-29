// Decompiled with JetBrains decompiler
// Type: TechGate.closeshift
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
    public class closeshift2
    {
        public static void ProcessMessage(NameValueCollection qparams, ref HttpContext Context)
        {
            string subject = Context.Request.ClientCertificate.Subject;
            GlobalObjectsManager.Logger.Info((object)("Subject:" + subject));
            string str1 = subject.Remove(0, subject.IndexOf("CN=") + "CN=".Length);
            str1.Remove(str1.IndexOf(","), str1.Length - str1.IndexOf(","));
            string serialNumber = Context.Request.ClientCertificate.SerialNumber;
            string str2 = int.Parse(serialNumber.Remove(0, serialNumber.Length - 11).Replace("-", ""), NumberStyles.HexNumber).ToString();
            qparams.Remove("function");
            qparams.Add("serial", str2);
            GlobalObjectsManager.Logger.Info("serial: " + str2);

            qparams.Remove("function");
            new DBManager(EtranConfigurationManager.DBConn).Execute("ShiftReceipt2_PUT", CommandType.StoredProcedure, DBManager.DataReadType.ExecuteNonQuery, qparams);
        }
    }
}
