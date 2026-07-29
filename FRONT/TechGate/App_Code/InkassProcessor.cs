// Decompiled with JetBrains decompiler
// Type: TechGate.InkassProcessor
// Assembly: TechGate, Version=1.0.4724.32104, Culture=neutral, PublicKeyToken=null
// MVID: 7D25CA8F-6EB1-4AE3-AFB7-31AA5758287A
// Assembly location: C:\PlaterraSvn\PROCESSING\FRONT\TechGate\bin\TechGate.dll

using System;
using EtranLib.Data;
using System.Collections;
using System.Collections.Specialized;
using System.Data;
using System.Globalization;
using System.Linq;
using System.Web;

namespace TechGate
{
    public class InkassProcessor
    {
        public static void ProcessMessage(NameValueCollection qparams, ref HttpContext Context)
        {
            string serialNumber = Context.Request.ClientCertificate.SerialNumber;
            string str1 = int.Parse(serialNumber.Remove(0, serialNumber.Length - 11).Replace("-", ""), NumberStyles.HexNumber).ToString();
            GlobalObjectsManager.Logger.Info((object)("InkassProcessor serial_number:" + str1));
            qparams.Add("serial_number", str1);
            string str2 = closeshift.DateFix(qparams["InkassDateTime"]);

            qparams["InkassDateTime"] = str2;
            qparams.Remove("function");
            if (qparams["note0"] == null)
            {
                qparams.Add("Currency", "0");
                qparams.Add("note0", "0");
                qparams.Add("note1", "0");
                qparams.Add("note2", qparams["note10"]);
                qparams.Remove("note10");
                qparams.Add("note3", qparams["note50"]);
                qparams.Remove("note50");
                qparams.Add("note4", qparams["note100"]);
                qparams.Remove("note100");
                qparams.Add("note5", qparams["note500"]);
                qparams.Remove("note500");
                qparams.Add("note6", qparams["note1000"]);
                qparams.Remove("note1000");
                qparams.Add("note7", qparams["note5000"]);
                qparams.Remove("note5000");
                qparams.Add("note8", qparams["note200"]);
                qparams.Remove("note200");
                qparams.Add("note9", qparams["note2000"]);
                qparams.Remove("note2000");
            }

            if (qparams["LastSumInkass"] == null)
                qparams.Add("LastSumInkass", "0");
            else if (qparams["LastSumInkass"].Length < 1)
                qparams["LastSumInkass"] = "0";


            // trubl xxx,xxxx
            var cntTotalSum = qparams["cntTotalSum"] ?? "";
            GlobalObjectsManager.Logger.Info("InkassProcessor cntTotalSum: " + cntTotalSum);
            if (!string.IsNullOrEmpty(cntTotalSum))
            {
                var cntTotalSumDecimal = decimal.Parse(cntTotalSum);
                qparams["cntTotalSum"] = ((int)Math.Round(cntTotalSumDecimal, 0)).ToString(CultureInfo.InvariantCulture);
                GlobalObjectsManager.Logger.Info("InkassProcessor FIX cntTotalSum: " + qparams["cntTotalSum"]);
            }


            DBManager dbManager = new DBManager(EtranConfigurationManager.DBConn);
            GlobalObjectsManager.Logger.Info((object)"InkassProcessor TRY EXEC INKASS_PUT6");
            Hashtable hashtable = (Hashtable)dbManager.Execute("INKASS_PUT6", CommandType.StoredProcedure, DBManager.DataReadType.Hashtable, qparams);

            GlobalObjectsManager.Logger.Info((object)"InkassProcessor INKASS_PUT6 OK");
            int num = int.Parse(hashtable[(object)"ID"].ToString());
            GlobalObjectsManager.Logger.Info((object)("InkassProcessor INKASS_PUT5 return ID " + (object)num));
            GlobalObjectsManager.Logger.Info((object)"InkassProcessor find Coins...");
            int TotalCoinCount = int.Parse(qparams["TotalCoinCount"]);
            int TotalCoinSum = int.Parse(qparams["TotalCoinSum"]);

            GlobalObjectsManager.Logger.Info("InkassProcessor " + TotalCoinCount + " TotalCoinSum " + TotalCoinSum);
            if (TotalCoinCount == 0)
                return;

            var source = qparams.AllKeys.Where(p => p.Trim().ToLower().StartsWith("coin")).ToList<string>();

            GlobalObjectsManager.Logger.Info((object)("InkassProcessor Coins found " + (object)Enumerable.Count<string>(source)));
            if (!Enumerable.Any<string>(source))
                return;
            GlobalObjectsManager.Logger.Info((object)"InkassProcessor Coins try process...");
            foreach (string str3 in source)
            {
                GlobalObjectsManager.Logger.Info((object)("InkassProcessor Coins process coin " + str3));
                string str4 = qparams[str3.ToString()];
                GlobalObjectsManager.Logger.Info((object)("InkassProcessor Coins process coin " + str3 + " coinCount" + str4));
                string str5 = str3.ToLower().Replace("coin", "");
                GlobalObjectsManager.Logger.Info((object)("InkassProcessor Coins process num " + num + " coinCount" + str4 + " coinCode" + str5));
                GlobalObjectsManager.Logger.Info((object)"InkassProcessor INKASS_PUT_COIN TRY...");

                dbManager.Execute("INKASS_PUT_COIN", CommandType.StoredProcedure, DBManager.DataReadType.ExecuteNonQuery, (NameValueCollection)null, (object)num, (object)str5, (object)str4);

                GlobalObjectsManager.Logger.Info((object)"InkassProcessor INKASS_PUT_COIN OK");
            }
        }
    }
}
