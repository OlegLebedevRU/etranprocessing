// Decompiled with JetBrains decompiler
// Type: GateGauge.SqlSp
// Assembly: GateGauge, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null
// MVID: 5E29B6EF-F709-4D1D-AAF0-FCC69378AC17
// Assembly location: C:\Новая папка\20160909\front\GateGauge\bin\GateGauge.dll

using System;
using System.Data;
using System.Data.SqlClient;

public class SqlSp
{
    public static bool GetKioskId(int sn, out int KioskId)
    {
        bool flag = false;
        KioskId = 0;
        try
        {
            SqlConnection connection = new SqlConnection(EtranConfigurationManager.DBConn);
            connection.Open();
            SqlCommand sqlCommand = new SqlCommand("GetTerminalIdBySn", connection);
            sqlCommand.CommandType = CommandType.StoredProcedure;
            sqlCommand.Parameters.Add("@sn", SqlDbType.Int);
            sqlCommand.Parameters["@sn"].Value = (object)sn;
            SqlDataReader sqlDataReader = sqlCommand.ExecuteReader();
            if (sqlDataReader.Read())
            {
                KioskId = int.Parse(sqlDataReader.GetValue(0).ToString());
                flag = true;
            }
            else
                KioskId = 0;
            sqlDataReader.Close();
            connection.Close();
        }
        catch (Exception ex)
        {
            flag = false;
            GlobalObjectsManager.Logger.Info((object)ex);
        }
        return flag;
    }

    public static bool GetKioskUpdateScript(int KioskId, int Number, string sysUpdateError, out string Script)
    {
        Script = (string)null;
        bool flag;
        try
        {
            SqlConnection connection = new SqlConnection(EtranConfigurationManager.DBConn);
            connection.Open();
            SqlCommand sqlCommand = new SqlCommand("GetKioskUpdateScript", connection);
            sqlCommand.CommandType = CommandType.StoredProcedure;
            sqlCommand.Parameters.Add("@KioskId", SqlDbType.Int);
            sqlCommand.Parameters["@KioskId"].Value = (object)KioskId;
            sqlCommand.Parameters.Add("@Number", SqlDbType.Int);
            sqlCommand.Parameters["@Number"].Value = (object)Number;
            sqlCommand.Parameters.Add("@sysUpdateError", SqlDbType.VarChar);
            sqlCommand.Parameters["@sysUpdateError"].Value = (object)sysUpdateError;
            SqlDataReader sqlDataReader = sqlCommand.ExecuteReader();
            if (sqlDataReader.Read())
                Script = sqlDataReader.GetValue(0).ToString();
            sqlDataReader.Close();
            connection.Close();
            flag = true;
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Info((object)ex);
            flag = false;
        }
        return flag;
    }
}
