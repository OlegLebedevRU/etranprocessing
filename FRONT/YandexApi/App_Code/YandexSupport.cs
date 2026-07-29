using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;
using System.Web;

/// <summary>
/// Summary description for YandexSupport
/// </summary>
public class YandexSupport
{
    private const string UriBase = "https://money.yandex.ru/";
    private readonly string _clientID;
    private readonly string _instanceName;
    private readonly string _redirectUri;
    private readonly string _clientSecret;


    public YandexSupport(int org_id)
    {

        var ds = GlobalObjectsManager.YandexWallet(org_id, "info", "");
        var info = ds.Tables[0].Rows[0]["Properties"].ToString();
        _clientID = ds.Tables[0].Rows[0]["ClientId"].ToString();
        _clientSecret = ds.Tables[0].Rows[0]["ClientSecret"].ToString();
        _instanceName = ds.Tables[0].Rows[0]["InstanceName"].ToString();
        _redirectUri = ds.Tables[0].Rows[0]["RedirectUrl"].ToString();
        //_redirectUri += "https://forpay.net/YandexApi/" + "?function=finish&org_id=" + org_id;
    }

    private string Post(string relativePath, string paramsPost)
    {
        GlobalObjectsManager.Logger.Info("PostIn: " + paramsPost);
        ServicePointManager.ServerCertificateValidationCallback = (a, b, c, d) => true;
        Uri uri = new Uri(UriBase);
        uri = uri.Append(relativePath);
        var ret =  WebRequest(uri.ToString(), 30000, paramsPost);
        GlobalObjectsManager.Logger.Info("PostOut: " + ret);
        return ret;
    }

    static public string WebRequest(string url, int timeout, string _data)
    {
        HttpWebRequest wrq = HttpWebRequest.Create(url) as HttpWebRequest;
        wrq.UserAgent = "Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.874.121 Safari/535.2";
        //if (addToken)
        //    wrq.Headers.Add("Authorization", _accessToken);

        wrq.Timeout = timeout;
        wrq.AllowAutoRedirect = false;
        if (_data == null)
            wrq.Method = "GET";
        else
        {
            wrq.Method = "POST";
            wrq.ContentType = "application/x-www-form-urlencoded";
            byte[] data = Encoding.UTF8.GetBytes(_data);
            wrq.ContentLength = data.Length;
            Stream newStream = wrq.GetRequestStream();
            newStream.Write(data, 0, data.Length);
            newStream.Close();
        }
        HttpWebResponse hwr = wrq.GetResponse() as HttpWebResponse;

        // check the header for a Location value
        var location = hwr.Headers["Location"];
        if (location != null)
        {
            GlobalObjectsManager.Logger.Info("location" + location);

            //Debug.WriteLine("loction: " + location);
            //return WebRequest(location, 30000, null);
            return location;
        }
        //else
        //{
        //    // anything non null means we got a redirect
        //    return string.Empty;
        //}
        Stream strm = hwr.GetResponseStream();
        StreamReader reader = new StreamReader(strm, System.Text.Encoding.UTF8);
        return reader.ReadToEnd();
    }

    public string GetToken(string code)
    {
        var portString = "client_id=" + _clientID + "&grant_type=authorization_code";
        portString += "&code=" + code;
        portString += "&client_secret=" + _clientSecret;
        portString += "&instance_name=" + _instanceName;
        portString += "&redirect_uri=" + HttpUtility.UrlEncode(_redirectUri);
        var r = Post("/oauth/token", portString);
        return r;
    }

    public string ActivationDone(int org_id, string accessToken)
    {
        try
        {
            var ds = GlobalObjectsManager.YandexWallet(org_id, "finish", accessToken);
            var info = ds.Tables[0].Rows[0]["Properties"].ToString();
            return info.Split('.')[0];
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error(ex);
        }
        return "";
    }

}
public static class UriExtensions
{
    public static Uri Append(this Uri uri, params string[] paths)
    {
        return new Uri(paths.Aggregate(uri.AbsoluteUri, (current, path) => string.Format("{0}/{1}", current.TrimEnd('/'), path.TrimStart('/'))));
    }
}
