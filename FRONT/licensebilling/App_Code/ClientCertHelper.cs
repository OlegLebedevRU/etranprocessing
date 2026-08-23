using System;
using System.Configuration;
using System.Data;
using System.Data.SqlClient;
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

        public static bool IsNewCA(HttpContext context)
        {
            string issuer = "";
            if (IsProxyMode(context))
            {
                issuer = context.Request.Headers["X-Client-Cert-Issuer-DN"] ?? "";
            }
            if (string.IsNullOrEmpty(issuer) && context.Request.ClientCertificate.IsPresent)
            {
                issuer = context.Request.ClientCertificate.Issuer ?? "";
            }
            return issuer.IndexOf("iot.leo4.ru", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        public static string GetDN(HttpContext context)
        {
            if (IsProxyMode(context))
            {
                string dn = context.Request.Headers["X-Client-Cert-DN"];
                if (!string.IsNullOrEmpty(dn)) return dn;
            }
            if (context.Request.ClientCertificate.IsPresent)
            {
                return context.Request.ClientCertificate.Subject ?? "";
            }
            return "";
        }

        public static string GetCN(HttpContext context) { return ParseDN(GetDN(context), "CN"); }
        public static string GetO(HttpContext context) { return ParseDN(GetDN(context), "O"); }
        public static string GetOU(HttpContext context) { return ParseDN(GetDN(context), "OU"); }

        public static int GetTerminalNumByOU(HttpContext context)
        {
            string ou = GetOU(context);
            if (string.IsNullOrEmpty(ou) || !ou.All(char.IsDigit))
                return 0;

            int deviceId;
            if (!int.TryParse(ou, out deviceId))
                return 0;

            try
            {
                using (SqlConnection connection = new SqlConnection(GlobalObjectsManager.DbConnectionString))
                {
                    connection.Open();
                    using (SqlCommand cmd = new SqlCommand("SELECT TOP 1 number FROM service..Kiosks WHERE number = @num", connection))
                    {
                        cmd.Parameters.Add("@num", SqlDbType.Int).Value = deviceId;
                        using (SqlDataReader reader = cmd.ExecuteReader())
                        {
                            if (reader.Read())
                            {
                                int res = int.Parse(reader.GetValue(0).ToString());
                                GlobalObjectsManager.Logger.Info("GetTerminalNumByOU: resolved terminal number=" + res + " for OU=" + ou);
                                return res;
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("GetTerminalNumByOU error:", ex);
            }
            return 0;
        }

        public static string GetDBSerialNumber(HttpContext context)
        {
            string serial = GetSerialNumber(context);

            if (!IsNewCA(context))
                return serial;

            string ou = GetOU(context);
            if (string.IsNullOrEmpty(ou) || !ou.All(char.IsDigit))
                return serial;

            int deviceId;
            if (!int.TryParse(ou, out deviceId))
                return serial;

            try
            {
                using (SqlConnection connection = new SqlConnection(GlobalObjectsManager.DbConnectionString))
                {
                    connection.Open();
                    using (SqlCommand cmd = new SqlCommand(
                        "SELECT TOP 1 c.serial_number FROM service..Certificates c JOIN service..Kiosks k ON c.kiosk_id = k.kiosk_id WHERE k.number = @num AND c.status_id < 3 ORDER BY c.update_datetime DESC",
                        connection))
                    {
                        cmd.Parameters.Add("@num", SqlDbType.Int).Value = deviceId;
                        using (SqlDataReader reader = cmd.ExecuteReader())
                        {
                            if (reader.Read())
                            {
                                string dbSerial = reader.GetValue(0).ToString();
                                GlobalObjectsManager.Logger.Info("GetDBSerialNumber: resolved serial=" + dbSerial + " for OU=" + ou);
                                return dbSerial;
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("GetDBSerialNumber error:", ex);
            }
            return serial;
        }

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
                    return t.Substring(field.Length + 1).Trim();
            }
            return null;
        }
    }
}
