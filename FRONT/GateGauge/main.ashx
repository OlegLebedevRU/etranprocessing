<%@ WebHandler Language="C#" Class="main" %>

using System;
using System.Data;
using System.Data.SqlClient;
using System.Globalization;
using System.IO;
using System.Text;
using System.Web;

public class main : IHttpHandler
{

    public void ProcessRequest(HttpContext Context)
    {
        try
        {
            GlobalObjectsManager.Logger.Info(Context.Request.Url);
            Context.Response.Charset = "windows-1251";
            Context.Response.ContentEncoding = Encoding.GetEncoding("windows-1251");
            Context.Response.ContentType = "text/xml";
            Context.Response.Cache.SetNoServerCaching();
            Context.Response.Cache.SetCacheability(HttpCacheability.NoCache);
            Context.Response.Cache.SetAllowResponseInBrowserHistory(false);
            GlobalObjectsManager.Logger.Info((object)"заполнили контекст");
            string query = (string)null;
            if (Context.Request.HttpMethod.ToUpper() == "POST")
            {
                Stream inputStream = Context.Request.InputStream;
                Encoding contentEncoding = Context.Request.ContentEncoding;
                StreamReader streamReader = new StreamReader(inputStream, contentEncoding);
                query = streamReader.ReadToEnd();
                inputStream.Close();
                streamReader.Close();
                GlobalObjectsManager.Logger.Info((object)"считали пост");
            }
            if (query != null)
            {
                GlobalObjectsManager.Logger.Info((object)("PostParams: " + query));
                string str1 = HttpUtility.ParseQueryString(query, Encoding.GetEncoding(1251))["GaugePack"];
                GlobalObjectsManager.Logger.Info((object)"разобрали стрингу");
                //string serialNumber = Context.Request.ClientCertificate.SerialNumber;
                //int sn = int.Parse(serialNumber.Remove(0, serialNumber.Length - 11).Replace("-", ""), NumberStyles.HexNumber);
                int sn = 0;
                int.TryParse(ClientCertHelper.GetSerialNumber(Context), out sn);
                GlobalObjectsManager.Logger.Info((object)("получили сн: " + sn));
                int terminalNum = sn != 0 ? this.GetTerminalNum(sn) : 0;
                GlobalObjectsManager.Logger.Info((object)("получили номер терма " + (object)terminalNum));
                if (terminalNum == 0 && ClientCertHelper.IsNewCA(Context))
                {
                    GlobalObjectsManager.Logger.Info((object)"Новый CA (iot.leo4.ru) detected, fallback to OU lookup");
                    terminalNum = ClientCertHelper.GetTerminalNumByOU(Context);
                    GlobalObjectsManager.Logger.Info((object)("OU lookup result: " + terminalNum));
                }
                if (terminalNum != 0)
                {
                    this.IX_PROCESS_TRANSACTION(terminalNum);
                    string str2 = str1.Substring(0, str1.Length - 1);
                    char[] chArray = new char[1]
            {
              ';'
            };
                    foreach (string str3 in str2.Split(chArray))
                    {
                        string s = str3.Split('=')[0];
                        string data = str3.Split('=')[1];
                        if (int.Parse(s) == 134)
                        {
                            try
                            {
                                new SendScriptsToKiosk(EtranConfigurationManager.SendScriptsToKioskUrl).SendScripts(terminalNum);
                                GlobalObjectsManager.Logger.Info((object)("Датчик 134 удачно обработан для терминала " + (object)terminalNum));
                            }
                            catch (Exception ex)
                            {
                                GlobalObjectsManager.Logger.Error((object)"Ошибка вызова функции 134 датчика", ex);
                            }
                        }
                        else
                            this.IX_PROCESS_RECORD(terminalNum, int.Parse(s), data);
                    }
                    this.IX_PROCESS_ENDTRANSACTION(terminalNum);
                }
                else
                {
                    GlobalObjectsManager.Logger.Error((object)"Возможно отозван сертификат");
                    Context.Response.Write("<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>Error</Result></Response>");
                    return;
                }
            }
            GlobalObjectsManager.Logger.Info((object)" RET: OK");
            Context.Response.Write("<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>OK</Result></Response>");
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error((object)"При обработке пакета возникла системная ошибка:", ex);
            Context.Response.Write("<?xml version = \"1.0\" encoding = \"windows-1251\"?><Response><Result>Error</Result></Response>");
        }
    }

    protected int GetTerminalNum(int sn)
    {
        int num = 0;
        try
        {
            SqlConnection connection = new SqlConnection(EtranConfigurationManager.DBConn);
            connection.Open();
            SqlCommand sqlCommand = new SqlCommand("GetTerminalNumBySn", connection);
            sqlCommand.CommandType = CommandType.StoredProcedure;
            sqlCommand.Parameters.Add("@sn", SqlDbType.Int);
            sqlCommand.Parameters["@sn"].Value = (object)sn;
            SqlDataReader sqlDataReader = sqlCommand.ExecuteReader();
            num = !sqlDataReader.Read() ? 0 : int.Parse(sqlDataReader.GetValue(0).ToString());
            sqlDataReader.Close();
            connection.Close();
        }
        catch
        {
        }
        return num;
    }

    public int IX_PROCESS_TRANSACTION(int id_term)
    {
        SqlConnection connection = (SqlConnection)null;
        try
        {
            string cmdText = "EXEC IX_PROCESS_TRANSACTION #id_term#,#id_db#,#ver#;".Replace("#id_term#", id_term.ToString()).Replace("#id_db#", "0").Replace("#ver#", "0");
            connection = new SqlConnection(EtranConfigurationManager.DBConn);
            SqlCommand sqlCommand = new SqlCommand(cmdText, connection);
            connection.Open();
            sqlCommand.ExecuteNonQuery();
            connection.Close();
            GlobalObjectsManager.Logger.Info((object)"IX_PROCESS_TRANSACTION OK");
            return 0;
        }
        catch (Exception ex)
        {
            if (connection != null)
                connection.Close();
            return 0;
        }
    }

    public int IX_PROCESS_RECORD(int id_term, int resource, string data)
    {
        SqlConnection connection = (SqlConnection)null;
        try
        {
            string cmdText = "EXEC IX_PROCESS_RECORD @id_term, @id_db,@ver,@id_term_pack,@time_size,@type_pack,@id_term_record,@resource,@type,@data;";
            connection = new SqlConnection(EtranConfigurationManager.DBConn);
            SqlCommand sqlCommand = new SqlCommand(cmdText, connection);
            SqlParameter sqlParameter1 = new SqlParameter("@id_term", SqlDbType.Int);
            sqlParameter1.Value = (object)id_term;
            sqlCommand.Parameters.Add(sqlParameter1);
            SqlParameter sqlParameter2 = new SqlParameter("@id_db", SqlDbType.Int);
            sqlParameter2.Value = (object)0;
            sqlCommand.Parameters.Add(sqlParameter2);
            SqlParameter sqlParameter3 = new SqlParameter("@ver", SqlDbType.Int);
            sqlParameter3.Value = (object)0;
            sqlCommand.Parameters.Add(sqlParameter3);
            SqlParameter sqlParameter4 = new SqlParameter("@id_term_pack", SqlDbType.Int);
            sqlParameter4.Value = (object)0;
            sqlCommand.Parameters.Add(sqlParameter4);
            SqlParameter sqlParameter5 = new SqlParameter("@time_size", SqlDbType.Int);
            sqlParameter5.Value = (object)0;
            sqlCommand.Parameters.Add(sqlParameter5);
            SqlParameter sqlParameter6 = new SqlParameter("@type_pack", SqlDbType.Int);
            sqlParameter6.Value = (object)0;
            sqlCommand.Parameters.Add(sqlParameter6);
            SqlParameter sqlParameter7 = new SqlParameter("@id_term_record", SqlDbType.Int);
            sqlParameter7.Value = (object)0;
            sqlCommand.Parameters.Add(sqlParameter7);
            SqlParameter sqlParameter8 = new SqlParameter("@resource", SqlDbType.Int);
            sqlParameter8.Value = (object)resource;
            sqlCommand.Parameters.Add(sqlParameter8);
            SqlParameter sqlParameter9 = new SqlParameter("@type", SqlDbType.Int);
            sqlParameter9.Value = (object)0;
            sqlCommand.Parameters.Add(sqlParameter9);
            SqlParameter sqlParameter10 = new SqlParameter("@data", SqlDbType.VarChar);
            sqlParameter10.Value = (object)data;
            sqlCommand.Parameters.Add(sqlParameter10);
            connection.Open();
            sqlCommand.ExecuteNonQuery();
            connection.Close();
            return 1;
        }
        catch
        {
            if (connection != null)
                connection.Close();
            return 0;
        }
    }

    public int IX_PROCESS_ENDTRANSACTION(int id_term)
    {
        SqlConnection connection = (SqlConnection)null;
        try
        {
            string cmdText = "EXEC IX_PROCESS_ENDTRANSACTION #id_term#,#id_db#,#ver#;".Replace("#id_term#", id_term.ToString()).Replace("#id_db#", "0").Replace("#ver#", "0");
            connection = new SqlConnection(EtranConfigurationManager.DBConn);
            SqlCommand sqlCommand = new SqlCommand(cmdText, connection);
            connection.Open();
            sqlCommand.ExecuteNonQuery();
            connection.Close();
            GlobalObjectsManager.Logger.Info((object)"IX_PROCESS_ENDTRANSACTION OK");
            return 0;
        }
        catch (Exception ex)
        {
            if (connection != null)
                connection.Close();
            return 0;
        }
    }

    public bool IsReusable
    {
        get
        {
            return false;
        }
    }

}
