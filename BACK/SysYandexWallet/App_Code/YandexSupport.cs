using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;

/// <summary>
/// Summary description for YandexSupport
/// </summary>
public class YandexSupport
{
    private readonly string _uriBase;
    private readonly string _clientID;
    private readonly string _instanceName;
    private readonly string _redirectUri;
    private readonly string _clientSecret;
    private readonly string _accessToken;


    public YandexSupport(string access_token)
    {
        _accessToken = "Bearer " + access_token;
        _clientID = GlobalObjectsManager._clientID;
        _clientSecret = GlobalObjectsManager._clientSecret;
        _instanceName = GlobalObjectsManager._instanceName;
        _uriBase = EtranConfigurationManager.UrlPay;
    }

    private string Post(string relativePath, string paramsPost)
    {
        GlobalObjectsManager.Logger.Info("PostIn: " + paramsPost);
        ServicePointManager.ServerCertificateValidationCallback = (a, b, c, d) => true;
        ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
        Uri uri = new Uri(_uriBase);
        uri = uri.Append(relativePath);
        var ret = WebRequest(uri.ToString(), 30000, paramsPost);
        GlobalObjectsManager.Logger.Info("PostOut: " + ret);
        return ret;
    }

    private string WebRequest(string url, int timeout, string _data)
    {
        HttpWebRequest wrq = HttpWebRequest.Create(url) as HttpWebRequest;
        wrq.UserAgent = "Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.874.121 Safari/535.2";
        wrq.Headers.Add("Authorization", _accessToken);

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
        StreamReader reader = new StreamReader(strm, Encoding.UTF8);
        return reader.ReadToEnd();
    }

    public string AccountInfo()
    {
        var r = Post("/api/account-info", "");
        return r;
    }

    public string ProcessPayment(string request_id)
    {
        var r = Post("/api/process-payment", "request_id=" + request_id);
        return r;
    }
    public string RequestPayment(string number, decimal amount)
    {
        var r = Post("/api/request-payment", "pattern_id=phone-topup&phone-number=" + number + "&amount=" + amount.ToString("F").Replace(',', '.'));
        //var r = Post("/api/request-payment", "pattern_id=phone-topup&phone-number=79162376067&amount=10.00");
        return r;
    }

}
public static class UriExtensions
{
    public static Uri Append(this Uri uri, params string[] paths)
    {
        return new Uri(paths.Aggregate(uri.AbsoluteUri, (current, path) => string.Format("{0}/{1}", current.TrimEnd('/'), path.TrimStart('/'))));
    }
}
