using System;
using System.Collections.Generic;
using System.Web;
using System.Text;
using System.Collections;
using System.Collections.Specialized;
using System.Data.SqlClient;
using System.Data;
using System.Xml;
using System.Security.Cryptography;
using System.Threading;
using System.Net;
using System.IO;
using System.Security.Cryptography.X509Certificates;


public class Collection
{
    static public NameValueCollection GetNameValueCollection(string data, string delimiter, string split)
    {
        string[] m = data.Split(delimiter.ToCharArray(0, delimiter.Length));
        NameValueCollection result = new NameValueCollection();
        foreach (string s in m)
        {
            string[] m1 = s.Split(split.ToCharArray(0, split.Length));
            if (m1.Length != 2) continue;
            result.Add(m1[0].Trim(), m1[1]);
        }
        return result;
    }
    static public NameValueCollection GetNameValueCollection(string Params)
    {
        NameValueCollection paramsTable = new NameValueCollection();
        string[] paramEntries = Params.Split(';');
        if (paramEntries.Length > 1 || paramEntries[0] != string.Empty)
        {
            foreach (string param in paramEntries)
            {
                int indx;
                string key = string.Empty;
                string val = string.Empty;

                try
                {
                    indx = param.IndexOf(' ');
                    if (indx < 0)
                        indx = param.IndexOf('=');

                    key = param.Substring(0, indx);
                    val = param.Substring(indx + 1, param.Length - indx - 1);
                }
                catch (Exception ex)
                {
                    int h = 0;
                }
                paramsTable.Add(key, val);
            }
        }
        return paramsTable;
    }

}


public class DBManager
{
    public enum DataReadType { DataSet, ExecuteNonQuery, Hashtable, ArrayList, ExecuteScalar, ExecuteXmlReader, NameValueCollection };

    SqlConnection connection = null;
    readonly string constr = string.Empty;
    SqlTransaction tran = null;
    SqlCommand command;

    public DBManager(string _constr)
    {
        constr = _constr;
        connection = new SqlConnection(constr);
        connection.Open();
        command = new SqlCommand(constr, connection);
    }

    public void Close()
    {
        connection.Close();
    }

    public void TranOpen()
    {
        tran = connection.BeginTransaction();
        command.Transaction = tran;
    }
    public void TranRollback()
    {
        tran.Rollback();
        command.Transaction = null;
    }

    public void TranCommit()
    {
        tran.Commit();
        command.Transaction = null;
    }


    public object Execute(string sp_name, CommandType ct, DataReadType rt, NameValueCollection namedValues, params object[] objValues)
    {

        command.CommandText = sp_name;
        command.CommandType = ct;

        if (ct == CommandType.StoredProcedure && (namedValues != null || objValues != null))
        {
            SqlCommandBuilder.DeriveParameters(command);
            int index = 0;
            foreach (SqlParameter parameter in command.Parameters)
            {
                if (parameter.Direction == ParameterDirection.Input || parameter.Direction == ParameterDirection.InputOutput)
                {
                    if (namedValues != null)
                    {
                        parameter.Value = namedValues[parameter.ParameterName.Replace("@", "")];
                    }
                    else
                    {
                        parameter.Value = objValues[index];
                    }
                    index++;
                }
            }
        }
        switch (rt)
        {

            case DataReadType.DataSet:
                {
                    SqlDataAdapter custDA = new SqlDataAdapter();
                    custDA.SelectCommand = command;
                    DataSet ds = new DataSet();
                    custDA.Fill(ds);
                    custDA.Dispose();
                    return ds;
                }

            case DataReadType.ExecuteNonQuery:
                {
                    return command.ExecuteNonQuery();
                }
            case DataReadType.NameValueCollection:
                {
                    NameValueCollection output = new NameValueCollection();
                    SqlDataReader reader = command.ExecuteReader();
                    if (reader.Read())
                        for (int i = 0; i < reader.FieldCount; i++)
                            output.Add(reader.GetName(i), reader[i].ToString());
                    reader.Close();
                    return output;
                }

            case DataReadType.Hashtable:
                {
                    Hashtable output = new Hashtable();
                    SqlDataReader reader = command.ExecuteReader();
                    if (reader.Read())
                        for (int i = 0; i < reader.FieldCount; i++)
                            output.Add(reader.GetName(i), reader[i]);
                    reader.Close();
                    return output;
                }
            case DataReadType.ArrayList:
                {
                    ArrayList output = new ArrayList();
                    SqlDataReader reader = command.ExecuteReader();
                    while (reader.Read())
                        output.Add(reader[0]);
                    reader.Close();
                    return output;
                }
            case DataReadType.ExecuteScalar:
                {
                    return command.ExecuteScalar();
                }
            case DataReadType.ExecuteXmlReader:
                {
                    XmlReader xmlr = command.ExecuteXmlReader();
                    xmlr.Read();
                    string xml_str = string.Empty;
                    while (xmlr.ReadState != System.Xml.ReadState.EndOfFile)
                    {
                        xml_str += xmlr.ReadOuterXml();
                    }
                    xmlr.Close();
                    return xml_str;
                }
        }
        return null;
    }
}
