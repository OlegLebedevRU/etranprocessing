using System;
using System.Data;
using System.Configuration;
using System.Web;
using System.Web.Security;
using System.Web.UI;
using System.Web.UI.HtmlControls;
using System.Web.UI.WebControls;
using System.Web.UI.WebControls.WebParts;
using EtranLib.Data;
using System.Collections.Specialized;
using System.Xml;


/// <summary>
/// Сводное описание для GetRek
/// </summary>
public class GetRek
{
	public GetRek()
	{
	}

    static public string getRek(string serial, string tsp, string amount)
    {

        DBManager db = new DBManager(ConfigurationManager.ConnectionStrings["Service"].ConnectionString);
        NameValueCollection hash = (NameValueCollection)db.Execute("GetRek_Select", CommandType.StoredProcedure, DBManager.DataReadType.NameValueCollection, null, serial, tsp, amount);
        XmlDocument xml_doc = new XmlDocument();
        xml_doc.LoadXml("<Root></Root>");
        XmlNode node = xml_doc.SelectSingleNode("/Root");
        //foreach (string key in hash.Keys)
        //{
        //    Response.Write(key + '=' + hash[key] + "<br>");
        //}


        EtranLib.Xml.XmlClass.InsertInnerText(xml_doc, node, hash);
        return xml_doc.OuterXml;
    }
}
