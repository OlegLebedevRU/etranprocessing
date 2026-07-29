using System;
using System.Threading;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using System.Data;
using CPX_SERVER;
using System.Reflection;
using CPX_IXENGINE;
using System.Data.SqlClient;

namespace CPX
{
    public class IXENGINE
    {
        
        public int IX_PROCESS_TRANSACTION(IXTRANS_UNPACK i)
        {
            string com = "";
           
            SqlConnection myConnection = null;
            SqlCommand myCommand = null;

            try
            {
                com = "EXEC IX_PROCESS_TRANSACTION #id_term#,#id_db#,#ver#;";
                com = com.Replace("#id_term#", i.TERM_ID.ToString());
                com = com.Replace("#id_db#", i.GUID_DB.ToString());
                com = com.Replace("#ver#", i.VER.ToString());

                myConnection = new SqlConnection(Share.mssql_connect);
                myCommand = new SqlCommand(com, myConnection);
                myConnection.Open();
                myCommand.ExecuteNonQuery();
                myConnection.Close();
                return 0;
            }
            catch (Exception e)
            {
                //Share.Log.Write("IXENGINE.cs 01:" + e.Message);
                if (myConnection != null)
                    myConnection.Close();
                return (0);
            }
        }


        public int IX_PROCESS_ENDTRANSACTION(IXTRANS_UNPACK i)
        {
            string com = "";

            SqlConnection myConnection = null;
            SqlCommand myCommand = null;

            try
            {
                com = "EXEC IX_PROCESS_ENDTRANSACTION #id_term#,#id_db#,#ver#;";
                com = com.Replace("#id_term#", i.TERM_ID.ToString());
                com = com.Replace("#id_db#", i.GUID_DB.ToString());
                com = com.Replace("#ver#", i.VER.ToString());

                myConnection = new SqlConnection(Share.mssql_connect);
                myCommand = new SqlCommand(com, myConnection);
                myConnection.Open();
                myCommand.ExecuteNonQuery();
                myConnection.Close();
                return 0;
            }
            catch (Exception e)
            {
                //Share.Log.Write("IXENGINE.cs 01:" + e.Message);
                if (myConnection != null)
                    myConnection.Close();
                return (0);
            }
        }
        
        public void Main(byte[] trans, ref byte[] ret)
        {
            try
            {
                //int ID_DB_TRANS = 0;

                ServerPluginsManager plugman = new ServerPluginsManager("ixEngine");
                IXTRANS_UNPACK ixtr = new IXTRANS_UNPACK(trans);


                IX_PROCESS_TRANSACTION(ixtr);
               // if (ID_DB_TRANS > 0)
               // {
                    plugman.PoolExec_ProcRecieve( ixtr);
                    IX_PROCESS_ENDTRANSACTION(ixtr);
               // }
                /*
                else
                {
                    //error
                }
                */
                IXTRANS_PACK t = null;

                t = new IXTRANS_PACK(ixtr.TERM_ID, 10);
                plugman.PoolExec_ProcSend(ref t);
                ret = t.GetBytes();
            }
            catch (Exception e)
            {
                //Share.Log.Write("IXENGINE.cs 02:" + e.Message);
            }
        }
    }
}
