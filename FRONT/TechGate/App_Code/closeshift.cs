// Decompiled with JetBrains decompiler
// Type: TechGate.closeshift
// Assembly: TechGate, Version=1.0.4724.32104, Culture=neutral, PublicKeyToken=null
// MVID: 7D25CA8F-6EB1-4AE3-AFB7-31AA5758287A
// Assembly location: C:\PlaterraSvn\PROCESSING\FRONT\TechGate\bin\TechGate.dll

using EtranLib.Data;
using System;
using System.Collections.Specialized;
using System.Data;
using System.Globalization;
using System.Web;

namespace TechGate
{
    public class closeshift
    {
        public static string DateFix(string date)
        {
            string str = date;
            try
            {
                if (date.IndexOf('/') > -1)
                {
                    IFormatProvider provider = (IFormatProvider)new CultureInfo("en-US", true);
                    str = DateTime.Parse(date, provider, DateTimeStyles.NoCurrentDateDefault).ToString("yyyy-MM-dd HH:mm:ss");
                }
            }
            catch (Exception ex)
            {
            }
            return str;
        }

        public static void ProcessMessage(NameValueCollection qparams, ref HttpContext Context)
        {
            string subject = Context.Request.ClientCertificate.Subject;
            GlobalObjectsManager.Logger.Info((object)("Subject:" + subject));
            string str1 = subject.Remove(0, subject.IndexOf("CN=") + "CN=".Length);
            str1.Remove(str1.IndexOf(","), str1.Length - str1.IndexOf(","));
            //string serialNumber = Context.Request.ClientCertificate.SerialNumber;
//string serialNumber = ClientCertHelper.GetSerialNumber(Context);
  //          string str2 = int.Parse(serialNumber.Remove(0, serialNumber.Length - 11).Replace("-", ""), NumberStyles.HexNumber).ToString();
string str2 = ClientCertHelper.GetSerialNumber(Context);
            qparams.Remove("function");
            qparams.Add("serial_number", str2);
            string str3 = closeshift.DateFix(qparams["CreateDate"]);
            string str4 = closeshift.DateFix(qparams["CloseDate"]);
            qparams["CreateDate"] = str3;
            qparams["CloseDate"] = str4;

            var cntTotalSum = qparams["cntTotalSum"] ?? "";
            GlobalObjectsManager.Logger.Info("closeshift cntTotalSum: " + cntTotalSum);
            if (!string.IsNullOrEmpty(cntTotalSum))
            {
                var cntTotalSumDecimal = decimal.Parse(cntTotalSum);
                qparams["cntTotalSum"] = ((int)Math.Round(cntTotalSumDecimal, 0)).ToString(CultureInfo.InvariantCulture);
                GlobalObjectsManager.Logger.Info("closeshift FIX cntTotalSum: " + qparams["cntTotalSum"]);
            }

            var TotalSum = qparams["TotalSum"] ?? "";
            GlobalObjectsManager.Logger.Info("closeshift TotalSum: " + TotalSum);
            if (!string.IsNullOrEmpty(TotalSum))
            {
                var TotalSumDecimal = decimal.Parse(TotalSum);
                qparams["TotalSum"] = ((int)Math.Round(TotalSumDecimal, 0)).ToString(CultureInfo.InvariantCulture);
                GlobalObjectsManager.Logger.Info("closeshift FIX TotalSum: " + qparams["TotalSum"]);
            }

            new DBManager(EtranConfigurationManager.DBConn).Execute("ShiftReceipt_PUT", CommandType.StoredProcedure, DBManager.DataReadType.ExecuteNonQuery, qparams);
        }

        private enum enCloseshift
        {
            TransactCount,
            InkassCount,
            InkassSum,
            TotalSum,
            CreateDate,
            CloseDate,
            LastPaymExtId,
            ReceiptExtId,
            cntInkass,
            cntInkassSum,
            cntTransact,
            cntTotalSum,
        }
    }
}
