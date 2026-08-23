using System;
using System.Configuration;
using System.Data;
using System.Data.SqlClient;
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

    public static bool IsNewCA(HttpContext ctx)
    {
        string issuer = "";
        if (IsProxyMode(ctx))
        {
            issuer = ctx.Request.Headers["X-Client-Cert-Issuer-DN"] ?? "";
        }
        if (string.IsNullOrEmpty(issuer) && ctx.Request.ClientCertificate.IsPresent)
        {
            issuer = ctx.Request.ClientCertificate.Issuer ?? "";
        }
        return issuer.IndexOf("iot.leo4.ru", StringComparison.OrdinalIgnoreCase) >= 0;
    }

    public static string GetDN(HttpContext ctx)
    {
        if (IsProxyMode(ctx))
        {
            string dn = ctx.Request.Headers["X-Client-Cert-DN"];
            if (!string.IsNullOrEmpty(dn)) return dn;
        }
        if (ctx.Request.ClientCertificate.IsPresent)
        {
            return ctx.Request.ClientCertificate.Subject ?? "";
        }
        return "";
    }

    public static string GetDNField(HttpContext ctx, string fieldName)
    {
        string dn = GetDN(ctx);
        if (string.IsNullOrEmpty(dn)) return "";
        foreach (string part in dn.Split(',', '/'))
        {
            string trimmed = part.Trim();
            if (trimmed.StartsWith(fieldName + "=", StringComparison.OrdinalIgnoreCase))
            {
                return trimmed.Substring(fieldName.Length + 1).Trim();
            }
        }
        return "";
    }

    public static int GetTerminalNumByOU(HttpContext ctx)
    {
        string ou = GetDNField(ctx, "OU");
        if (string.IsNullOrEmpty(ou) || !ou.All(char.IsDigit))
            return 0;

        int deviceId;
        if (!int.TryParse(ou, out deviceId))
            return 0;

        try
        {
            using (SqlConnection connection = new SqlConnection(EtranConfigurationManager.DBConn))
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

    public static int GetKioskIdByOU(HttpContext ctx)
    {
        string ou = GetDNField(ctx, "OU");
        if (string.IsNullOrEmpty(ou) || !ou.All(char.IsDigit))
            return 0;

        int deviceId;
        if (!int.TryParse(ou, out deviceId))
            return 0;

        try
        {
            using (SqlConnection connection = new SqlConnection(EtranConfigurationManager.DBConn))
            {
                connection.Open();
                using (SqlCommand cmd = new SqlCommand("SELECT TOP 1 kiosk_id FROM service..Kiosks WHERE number = @num", connection))
                {
                    cmd.Parameters.Add("@num", SqlDbType.Int).Value = deviceId;
                    using (SqlDataReader reader = cmd.ExecuteReader())
                    {
                        if (reader.Read())
                        {
                            int res = int.Parse(reader.GetValue(0).ToString());
                            GlobalObjectsManager.Logger.Info("GetKioskIdByOU: resolved kiosk_id=" + res + " for OU=" + ou);
                            return res;
                        }
                    }
                }
            }
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error("GetKioskIdByOU error:", ex);
        }
        return 0;
    }

    public static string GetDBSerialNumber(HttpContext ctx)
    {
        string serial = GetSerialNumber(ctx);

        if (!IsNewCA(ctx))
            return serial;

        string ou = GetDNField(ctx, "OU");
        if (string.IsNullOrEmpty(ou) || !ou.All(char.IsDigit))
            return serial;

        int deviceId;
        if (!int.TryParse(ou, out deviceId))
            return serial;

        try
        {
            using (SqlConnection connection = new SqlConnection(EtranConfigurationManager.DBConn))
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

    public static bool IsVerified(HttpContext ctx)
    {
        if (IsProxyMode(ctx))
        {
            string v = ctx.Request.Headers["X-Client-Cert-Verified"];
            return !string.IsNullOrEmpty(v) && v.ToUpper() == "SUCCESS";
        }
        return ctx.Request.ClientCertificate.IsPresent;
    }
}
