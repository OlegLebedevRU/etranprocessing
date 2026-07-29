using System;
using System.Data;
using System.Configuration;
using System.Web;
using System.Web.Security;
using System.Web.UI;
using System.Web.UI.HtmlControls;
using System.Web.UI.WebControls;
using System.Web.UI.WebControls.WebParts;
using System.Text;
using System.IO;
using System.Data;
using System.Security.Cryptography;


/// <summary>
/// Сводное описание для Signature
/// </summary>
public class Signature
{

    public static string MD5HashHex(string data)
    {
        byte[] result = (new MD5CryptoServiceProvider()).ComputeHash(Encoding.GetEncoding(1251).GetBytes(data));
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < result.Length; i++)
        {
            sb.Append(result[i].ToString("X2"));
        }
        return sb.ToString();
    }

    static public string EncodeTo64(string toEncode)
    {
        byte[] toEncodeAsBytes
              = System.Text.ASCIIEncoding.ASCII.GetBytes(toEncode);
        string returnValue
              = System.Convert.ToBase64String(toEncodeAsBytes);
        return returnValue;
    }

    static public string DecodeFrom64(string encodedData)
    {
        byte[] encodedDataAsBytes
            = System.Convert.FromBase64String(encodedData);
        string returnValue =
           System.Text.ASCIIEncoding.ASCII.GetString(encodedDataAsBytes);
        return returnValue;
    }

    static public string GetSign(string _tosign)
    {
        GlobalObjectsManager.Logger.Info("tosign ...");
        if (_tosign == null)
            throw new Exception("Не обнаружено исходной информации.");
        else
            if (_tosign.Length < 1)
                throw new Exception("Не обнаружено исходной информации.");

        string tosign = DecodeFrom64(_tosign);
        string _tohash = tosign + EtranConfigurationManager.SignKey;
        GlobalObjectsManager.Logger.Info("_tohash: " + _tohash);
        string md5hash = MD5HashHex(_tohash);
        GlobalObjectsManager.Logger.Info("md5hash: " + md5hash);
        return md5hash;
        //string ret = XEnrollClass.MakeXmlResponse(XEnrollClass.eTypeResponses.OK, "<sign>" + md5hash + "</sign>");
        //GlobalObjectsManager.Logger.Info("ret: " + ret);
        //Context.Response.Write(ret);

    }
}
