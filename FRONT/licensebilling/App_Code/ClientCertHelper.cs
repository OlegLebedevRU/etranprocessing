using System;
using System.Configuration;
using System.Linq;
using System.Web;
namespace Dispatcher
{
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
                        ? new string[0]
                        : configured.Split(',').Select(s => s.Trim()).ToArray();
                }
                return _trustedProxies;
            }
        }
        private static bool IsFromTrustedProxy(HttpContext context)
        {
            if (TrustedProxies.Length == 0) return false;
            return TrustedProxies.Any(tp => tp == context.Request.UserHostAddress);
        }
        public static bool IsProxyMode(HttpContext context)
        {
            return IsFromTrustedProxy(context)
                && !string.IsNullOrEmpty(context.Request.Headers["X-Client-Cert-Serial"]);
        }
        public static string GetSerialNumber(HttpContext context)
{
    if (IsProxyMode(context))
    {
        string serial = context.Request.Headers["X-Client-Cert-Serial"];
        serial = serial.Replace(":", "").Replace("-", "");
        // last 8 hex characters = CA request ID (INT32)
        if (serial.Length >= 8)
        {
            string last8 = serial.Substring(serial.Length - 8);
            return long.Parse(last8, System.Globalization.NumberStyles.HexNumber).ToString();
        }
        return serial;
    }
    if (context.Request.ClientCertificate.IsPresent
        && context.Request.ClientCertificate.SerialNumber != null
        && context.Request.ClientCertificate.SerialNumber.Length >= 11)
    {
        string s = context.Request.ClientCertificate.SerialNumber;
        s = s.Remove(0, s.Length - 11).Replace("-", "");
        return int.Parse(s, System.Globalization.NumberStyles.HexNumber).ToString();
    }
    return "";
}
        public static string GetDN(HttpContext context)
        {
            if (IsProxyMode(context))
            {
                string dn = context.Request.Headers["X-Client-Cert-DN"];
                if (!string.IsNullOrEmpty(dn)) return dn;
            }
            return context.Request.ClientCertificate.Subject;
        }
        public static string GetCN(HttpContext context) { return ParseDN(GetDN(context), "CN"); }
        public static string GetO(HttpContext context) { return ParseDN(GetDN(context), "O"); }
        public static string GetOU(HttpContext context) { return ParseDN(GetDN(context), "OU"); }
        public static bool IsVerified(HttpContext context)
        {
            if (IsProxyMode(context))
            {
                string v = context.Request.Headers["X-Client-Cert-Verified"];
                return !string.IsNullOrEmpty(v) && v.ToUpper() == "SUCCESS";
            }
            return context.Request.ClientCertificate.IsPresent;
        }
        private static string ParseDN(string dn, string field)
        {
            if (string.IsNullOrEmpty(dn)) return null;
            foreach (string part in dn.Split(',', '/'))
            {
                string t = part.Trim();
                if (t.StartsWith(field + "=", StringComparison.OrdinalIgnoreCase))
                    return t.Substring(field.Length + 1);
            }
            return null;
        }
    }
}
