using System;
using System.Data;
using System.Configuration;
using System.Web;
using System.Web.Security;
using System.Web.UI;
using System.Web.UI.HtmlControls;
using System.Web.UI.WebControls;
using System.Web.UI.WebControls.WebParts;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.IO;
using System.Xml;
using System.Collections;
using System.Collections.Specialized;

/// <summary>
/// Сводное описание для Reestr
/// </summary>
public class Reestr
{
	public Reestr()
	{
	}


    private static readonly string m_reestr_req =
"<?xml version=\"1.0\" encoding=\"windows-1251\"?>" +
"<request>" +
"<auth login=\"" + "%login%" + "\" sign=\"" + "%sign%" + "\" signAlg=\"MD5\"/>" +
"<client terminal=\"" + "%terminalid%" + "\" software=\"Dealer v0\"/>"
+ "<reports><getPayments mode=\"async\">"
+ @"<date-from>%from%</date-from>
<date-to>%to%</date-to>
</getPayments>
</reports>"
+ "</request>";



    /// 
    /// Gets the response with post.
    /// 
    /// <param name="StrURL">The URL.
    /// <param name="strPostData">The post data.
    /// HTML Result
    protected static string GetResponseWithPost(string StrURL, string strPostData)
    {
        string strReturn = "";
        HttpWebRequest objRequest = null;
        ASCIIEncoding objEncoding = new ASCIIEncoding();
        Stream reqStream = null;
        HttpWebResponse objResponse = null;
        StreamReader objReader = null;
        try
        {
            objRequest = (HttpWebRequest)WebRequest.Create(StrURL);

            objRequest.Method = "POST";
            byte[] objBytes = objEncoding.GetBytes(strPostData);
            objRequest.ContentLength = objBytes.Length;
            objRequest.ContentType = "application/x-www-form-urlencoded";
            reqStream = objRequest.GetRequestStream();
            reqStream.Write(objBytes, 0, objBytes.Length);

            IAsyncResult ar = objRequest.BeginGetResponse(new AsyncCallback(GetScrapingResponse), objRequest);
            //// Wait for request to complete
            ar.AsyncWaitHandle.WaitOne(1000 * 60 * 3, true);
            if (objRequest.HaveResponse == false)
            {
                throw new Exception("No Response!!!");
            }
            objResponse = (HttpWebResponse)objRequest.EndGetResponse(ar);
            objReader = new StreamReader(objResponse.GetResponseStream());
            strReturn = objReader.ReadToEnd();

        }
        catch (Exception exp)
        {
            throw exp;
        }
        finally
        {
            objRequest = null;
            objEncoding = null;
            reqStream = null;
            if (objResponse != null)
                objResponse.Close();
            objResponse = null;
            objReader = null;
        }
        return strReturn;
    }

    /// 
    /// Gets the scraping response.
    /// 
    /// <param name="result">The result.
    protected static void GetScrapingResponse(IAsyncResult result)
    {

    }

    static public string GetReestr(DateTime dt_from, DateTime dt_to, string Rek)
    {
        try
        {
            string reestr_req = m_reestr_req;

            NameValueCollection m_Rek = EtranLib.Collection.Collection.GetNameValueCollection(Rek, ";", "=");
            string rrr = m_Rek["rek"];
            string[] Splitter = (m_Rek["rek"] != null) ? m_Rek["rek"].Split(':') : Rek.Split(';');

            reestr_req = reestr_req.Replace("%terminalid%", Splitter[0]);
            reestr_req = reestr_req.Replace("%login%", Splitter[1]);
            reestr_req = reestr_req.Replace("%sign%", EtranLib.Crypto.Crypto.MD5HashHex(Splitter[2]));

            string from = dt_from.ToString("yyyy-MM-ddTHH:mm:ss");
            string to = dt_to.ToString("yyyy-MM-ddTHH:mm:ss");

            reestr_req = reestr_req.Replace("%from%", from);
            reestr_req = reestr_req.Replace("%to%", to);
            XmlDocument doc = new XmlDocument();
            doc.LoadXml(reestr_req);
            string msg = doc.OuterXml;
            doc.Save(GlobalObjectsManager.curr_path + Splitter[0] + "-" + dt_from.ToString("yyyyMMdd") + "-" + dt_to.ToString("yyyyMMdd") + "-SEND-" + ".xml");
            string ret = GetResponseWithPost(EtranConfigurationManager.PaymentUrl, msg);
            doc.LoadXml(ret);
            doc.Save(GlobalObjectsManager.curr_path + Splitter[0] + "-" + dt_from.ToString("yyyyMMdd") + "-" + dt_to.ToString("yyyyMMdd") + "-RECV-" + ".xml");
        }
        catch (Exception ex)
        {
            return ex.Message;
        }
        return "OK";
    }
}
