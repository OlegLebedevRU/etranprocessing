// Decompiled with JetBrains decompiler
// Type: TechGate.getshiftreport
// Assembly: TechGate, Version=1.0.4724.32104, Culture=neutral, PublicKeyToken=null
// MVID: 7D25CA8F-6EB1-4AE3-AFB7-31AA5758287A
// Assembly location: C:\PlaterraSvn\PROCESSING\FRONT\TechGate\bin\TechGate.dll

using EtranLib.Collection;
using EtranLib.Data;
using System;
using System.Collections;
using System.Collections.Specialized;
using System.Data;
using System.Globalization;
using System.Web;
using System.Xml;

namespace TechGate
{
  public class getshiftreport
  {
    private static XmlElement CreateElement(XmlDocument doc, string el_name, NameValueCollection attr)
    {
      return getshiftreport.CreateElement(doc, el_name, attr, (string) null);
    }

    private static XmlElement CreateElement(XmlDocument doc, string el_name, NameValueCollection attr, string InnerText)
    {
      XmlElement element = doc.CreateElement(el_name);
      if (InnerText != null)
        element.InnerText = InnerText;
      if (attr != null)
      {
        for (int index = 0; index < attr.Count; ++index)
        {
          XmlAttribute attribute = doc.CreateAttribute(attr.Keys[index]);
          attribute.Value = attr[index];
          element.Attributes.Append(attribute);
        }
      }
      return element;
    }

    private static void InsertInnerText(XmlDocument xmldoc, XmlNode xmlnode, NameValueCollection InnerText)
    {
      IEnumerator enumerator = InnerText.GetEnumerator();
      enumerator.Reset();
      while (enumerator.MoveNext())
      {
        string el_name = (string) enumerator.Current;
        string InnerText1 = InnerText[el_name];
        XmlElement element = getshiftreport.CreateElement(xmldoc, el_name, (NameValueCollection) null, InnerText1);
        xmlnode.AppendChild((XmlNode) element);
      }
    }

    public static string DbNull_String(object val)
    {
      return val.ToString();
    }

    public static string ProcessMessage(NameValueCollection qparams, ref HttpContext Context)
    {
      string subject = Context.Request.ClientCertificate.Subject;
      GlobalObjectsManager.Logger.Info((object) ("Subject:" + subject));
      NameValueCollection nameValueCollection = Collection.GetNameValueCollection(subject, ",", "=");
      string str1 = nameValueCollection["E"];
      string s = nameValueCollection["O"];
      int num1 = int.Parse(s);
      GlobalObjectsManager.Logger.Info((object) ("O: " + s + " E: " + str1));
      string str2 = "org@e-transfer.ru";
      if (str1.IndexOf(str2) == -1)
        throw new Exception("ошибка сертификата");
      string str3 = subject.Remove(0, subject.IndexOf("CN=") + "CN=".Length);
      GlobalObjectsManager.Logger.Info((object) ("cn:" + str3.Remove(str3.IndexOf(","), str3.Length - str3.IndexOf(","))));
     // string serialNumber = Context.Request.ClientCertificate.SerialNumber;
string serialNumber = ClientCertHelper.GetSerialNumber(Context);
//      GlobalObjectsManager.Logger.Info((object) ("SerialNumber:" + int.Parse(serialNumber.Remove(0, serialNumber.Length - 11).Replace("-", ""), NumberStyles.HexNumber).ToString()));
      GlobalObjectsManager.Logger.Info((object) ("SerialNumber:" + serialNumber));
      int num2 = qparams["function"] == "getshiftreport" ? 1 : 2;
      int num3 = int.Parse(qparams["KioskNumber"].ToString());
      DateTime dateTime1 = DateTime.Parse(qparams["ReportDateBegin"].ToString());
      DateTime dateTime2 = DateTime.Parse(qparams["ReportDateEnd"].ToString());
      DataSet dataSet = (DataSet) new DBManager(EtranConfigurationManager.DBConn).Execute("ShiftReceipt_Get", CommandType.StoredProcedure, DBManager.DataReadType.DataSet, (NameValueCollection) null, (object) num3, (object) num2, (object) dateTime1, (object) dateTime2, (object) num1);
      int.Parse(dataSet.Tables[0].Rows[0]["kiosk_id"].ToString());
      string str4 = getshiftreport.DbNull_String(dataSet.Tables[0].Rows[0]["serial_number"]);
      string str5 = getshiftreport.DbNull_String(dataSet.Tables[0].Rows[0]["sn_printer"]);
      string str6 = getshiftreport.DbNull_String(dataSet.Tables[0].Rows[0]["address"]);
      string str7 = getshiftreport.DbNull_String(dataSet.Tables[2].Rows[0]["org_name"]);
      string str8 = getshiftreport.DbNull_String(dataSet.Tables[2].Rows[0]["address"]);
      string str9 = getshiftreport.DbNull_String(dataSet.Tables[2].Rows[0]["inn"]);
      XmlDocument xmldoc = new XmlDocument();
      XmlElement element1 = xmldoc.CreateElement("Reports");
      xmldoc.AppendChild((XmlNode) element1);
      NameValueCollection InnerText = new NameValueCollection();
      InnerText.Add("KioskNumber", num3.ToString());
      InnerText.Add("KioskSerialNumber", str4);
      InnerText.Add("PrinterSerialNumber", str5);
      InnerText.Add("KioskAddress", str6);
      InnerText.Add("OrgName", str7);
      InnerText.Add("OrgAddrress", str8);
      InnerText.Add("OrgInn", str9);
      getshiftreport.InsertInnerText(xmldoc, (XmlNode) element1, InnerText);
      getshiftreport.enGetshiftreport enGetshiftreport = getshiftreport.enGetshiftreport.ZReportNumber;
      getshiftreport.enGetinkassreport enGetinkassreport = getshiftreport.enGetinkassreport.InkassData;
      for (int index = 0; index < dataSet.Tables[1].Rows.Count; ++index)
      {
        InnerText.Clear();
        foreach (string name in Enum.GetNames(num2 == 1 ? enGetshiftreport.GetType() : enGetinkassreport.GetType()))
        {
          string str10 = dataSet.Tables[1].Rows[index][name].ToString();
          InnerText.Add(name, str10);
        }
        XmlElement element2 = xmldoc.CreateElement("Report");
        element1.AppendChild((XmlNode) element2);
        getshiftreport.InsertInnerText(xmldoc, (XmlNode) element2, InnerText);
      }
      return xmldoc.OuterXml;
    }

    private enum enGetshiftreport
    {
      ZReportNumber,
      TotalSum,
      LastTotalSum,
      cntTotalSum,
      CloseDate,
      CreateDate,
    }

    private enum enGetinkassreport
    {
      InkassData,
      note0,
      note1,
      note2,
      note3,
      note4,
      note5,
      note6,
      note7,
      LastSumInkass,
      cntInkassSum,
    }
  }
}
