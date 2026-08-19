using System;
using System.Configuration;
using System.Linq;
using System.Web;
public static class ClientCertHelper
{
    private static string[] _trustedProxies;
    private static string[] TrustedProxies
    {
        get
        {
            if (_trustedProxies == null)
            {
                string configured = ConfigurationManager.AppSettings["TrustedProxyIP"];
                _trustedProxies = string.IsNullOrEmpty(configured)
                    ? new string[0] : configured.Split(',').Select(s => s.Trim()).ToArray();
            }
            return _trustedProxies;
        }
    }
    private static bool IsFromTrustedProxy(HttpContext ctx)
    {
        if (TrustedProxies.Length == 0) return false;
        return TrustedProxies.Any(tp => tp == ctx.Request.UserHostAddress);
    }
    public static bool IsProxyMode(HttpContext ctx)
    {
        return IsFromTrustedProxy(ctx)
            && !string.IsNullOrEmpty(ctx.Request.Headers["X-Client-Cert-Serial"]);
    }
    public static string GetSerialNumber(HttpContext ctx)
    {
        if (IsProxyMode(ctx))
        {
            string s = ctx.Request.Headers["X-Client-Cert-Serial"].Replace(":", "").Replace("-", "");
            if (s.Length >= 8)
                return long.Parse(s.Substring(s.Length - 8), System.Globalization.NumberStyles.HexNumber).ToString();
            return s;
        }
        if (ctx.Request.ClientCertificate.IsPresent
            && ctx.Request.ClientCertificate.SerialNumber != null
            && ctx.Request.ClientCertificate.SerialNumber.Length >= 11)
        {
            string s = ctx.Request.ClientCertificate.SerialNumber;
            s = s.Remove(0, s.Length - 11).Replace("-", "");
            return int.Parse(s, System.Globalization.NumberStyles.HexNumber).ToString();
        }
        return "";
    }

    /// <summary>
    /// Same as GetSerialNumber, but throws a clear, descriptive exception
    /// instead of silently returning an empty string when no client
    /// certificate is present (direct flow) and no trusted proxy header is
    /// present either. Use this at call sites where the result is later
    /// parsed as an integer or used as a mandatory DB parameter - an empty
    /// string there used to fail deep inside framework code (e.g.
    /// FormatException from SqlParameter/int.Parse) with no indication of
    /// the real cause.
    /// </summary>
    public static string GetSerialNumberOrThrow(HttpContext ctx)
    {
        string serial = GetSerialNumber(ctx);
        if (string.IsNullOrEmpty(serial))
            throw new Exception("Missing or invalid client certificate: cannot determine serial number.");
        return serial;
    }
}
