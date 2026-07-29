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


namespace Crypto
{
    using System;
    using System.Text;
    using System.Security.Cryptography;
    using System.Security.Cryptography.X509Certificates;
    using System.Collections;
    using System.Collections.Specialized;

    public class Crypto
    {

        public static void CleanStoreByOU(int ou_term_number, string Rek)
        {
            X509Store store = null;
            try
            {
                store = new X509Store(StoreName.My, StoreLocation.CurrentUser);
                store.Open(OpenFlags.MaxAllowed);
                X509Certificate2Collection collection = (X509Certificate2Collection)store.Certificates;
                X509Certificate2Collection found = collection.Find(X509FindType.FindBySubjectName, Rek, false);
                foreach (X509Certificate2 cert in found)
                {
                    NameValueCollection nvc_orgs = Collection.GetNameValueCollection(cert.Subject, ",", "=");
                    string tm = nvc_orgs["OU"];
                    int i_tm = 0;
                    try
                    {
                        i_tm = int.Parse(tm);
                    }
                    catch
                    {
                    }
                    if (ou_term_number == i_tm)
                    {
                        store.Remove(cert);
                    }


                }

            }
            finally
            {
                if (store != null)
                    store.Close();
            }
        }

        static public void AddCet(X509Certificate2 cert)
        {
            X509Store store = null;
            try
            {
                store = new X509Store(StoreName.My, StoreLocation.CurrentUser);
                store.Open(OpenFlags.MaxAllowed);
                store.Add(cert);
            }
            finally
            {
                store.Close();
            }
        }


        static public ArrayList CertListSubject(string Rek)
        {
            X509Store store = null;
            try
            {
                store = new X509Store(StoreName.My, StoreLocation.CurrentUser);
                store.Open(OpenFlags.ReadOnly);
                X509Certificate2Collection collection = (X509Certificate2Collection)store.Certificates;
                X509Certificate2Collection found = collection.Find(X509FindType.FindBySubjectName, Rek, true);
                ArrayList list = new ArrayList();
                for (int i = 0; i < found.Count; i++)
                {
                    list.Add(found[i].Subject);
                }
                return list;
            }
            catch //(Exception ex)
            {
                //GlobalObjectsManager.Logger.Error("При доступе к пользовательскому сертификату возникло исключение", ex);
            }
            finally
            {
                store.Close();
            }
            return null;
        }

        static public X509Certificate2Collection GetCertColl(string Rek)
        {
            X509Store store = null;
            try
            {
                store = new X509Store(StoreName.My, StoreLocation.CurrentUser);
                store.Open(OpenFlags.ReadOnly);
                X509Certificate2Collection collection = (X509Certificate2Collection)store.Certificates;
                return collection.Find(X509FindType.FindBySubjectName, Rek, true);
            }
            catch //(Exception ex)
            {
                //GlobalObjectsManager.Logger.Error("При доступе к пользовательскому сертификату возникло исключение", ex);
            }
            finally
            {
                store.Close();
            }
            return null;
        }

        static public X509Certificate2 GetCertByString(string Rek)
        {
            X509Certificate2 cert = null;
            X509Store store = null;
            try
            {
                store = new X509Store(StoreName.My, StoreLocation.LocalMachine);
                store.Open(OpenFlags.ReadOnly);
                X509Certificate2Collection collection = (X509Certificate2Collection)store.Certificates;
                X509Certificate2Collection found = collection.Find(X509FindType.FindBySubjectName, Rek, false);
                if (found.Count == 1)
                    cert = found[0];
                else
                    if (found.Count > 1)
                    throw new Exception("найдено больше одного сертификата удовлетворяющему условию: " + Rek);
                else
                    throw new Exception("не найдено сертификата удовлетворяющему условию: " + Rek);


            }
            catch //(Exception ex)
            {
                //GlobalObjectsManager.Logger.Error("При доступе к пользовательскому сертификату возникло исключение", ex);
            }
            finally
            {
                store.Close();
            }
            return cert;
        }



        static public bool VerifyHash(string msg, string signature, string certrek)
        {
            try
            {
                X509Certificate2 cert = GetCertByString(certrek);
                return VerifyHash(msg, signature, ref cert);
            }
            catch (Exception e)
            {
                string f = e.Message;
            }
            return false;
        }


        static public string HashAndSign(string msg, string certrek, int indx)
        {
            try
            {
                X509Certificate2 cert = GetCertColl(certrek)[indx];
                return HashAndSign(msg, ref cert);
            }
            catch
            {
            }
            return null;
        }

        static public string HashAndSign(string msg, string certrek)
        {
            try
            {
                X509Certificate2 cert = GetCertByString(certrek);
                return HashAndSign(msg, ref cert);
            }
            catch
            {
            }
            return null;
        }

        static public bool VerifyHash(string msg, string signature, ref X509Certificate2 cert)
        {
            RSACryptoServiceProvider rsa_public = cert.PublicKey.Key as RSACryptoServiceProvider;
            return rsa_public.VerifyHash((new SHA1Managed()).ComputeHash(Encoding.Default.GetBytes(msg)), CryptoConfig.MapNameToOID("SHA1"), Convert.FromBase64String(signature));
        }

        static public string HashAndSign(string msg, ref X509Certificate2 cert)
        {
            RSACryptoServiceProvider rsa_private = cert.PrivateKey as RSACryptoServiceProvider;
            HashAlgorithm SHA1 = HashAlgorithm.Create("SHA1");
            return Convert.ToBase64String(rsa_private.SignHash(SHA1.ComputeHash(Encoding.Default.GetBytes(msg)), CryptoConfig.MapNameToOID("SHA1")));
        }

        public static string MD5Base64(string data)
        {
            return Convert.ToBase64String((new MD5CryptoServiceProvider()).ComputeHash(Encoding.GetEncoding(1251).GetBytes(data)));
        }

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
    }
}


