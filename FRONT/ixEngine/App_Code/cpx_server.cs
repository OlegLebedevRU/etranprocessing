using System;
using System.IO;
using System.Text;
using System.Runtime.InteropServices;
using System.Reflection;
using System.Data.SqlClient;
using CPX_IXENGINE;

namespace CPX_SERVER
{
    using WORD = System.UInt16;
    using DWORD = System.UInt32;


    //---------------------------------------------------------------
    // Общие классы для упаковки/распаковки транзакций
    //---------------------------------------------------------------
    public class UTIL_TYPES
    {
        public void set_ushort(object obj, MemoryStream ms, ref byte[] ret)
        {
            ret = BitConverter.GetBytes((ushort)obj);
            ms.Write(ret, 0, Marshal.SizeOf(obj));
        }
        public void set_byte(object obj, MemoryStream ms, ref byte[] ret)
        {
            ret = BitConverter.GetBytes((byte)obj);
            ms.Write(ret, 0, Marshal.SizeOf(obj));
        }

        public void set_uint(object obj, MemoryStream ms, ref byte[] ret)
        {
            ret = BitConverter.GetBytes((uint)obj);
            ms.Write(ret, 0, Marshal.SizeOf(obj));
        }

        public ushort get_ushort(object obj, byte[] m, ref int c)
        {
            return ((ushort)BitConverter.ToUInt16(GetVar(Marshal.SizeOf(obj), m, ref c), 0));
        }
        public uint get_uint(object obj, byte[] m, ref int c)
        {
            return ((uint)BitConverter.ToUInt32(GetVar(Marshal.SizeOf(obj), m, ref c), 0));
        }
        public byte get_byte(object obj, byte[] m, ref int c)
        {
            return ((byte)GetVar(Marshal.SizeOf(obj), m, ref c)[0]);
        }
        public byte[] GetVar(int size, byte[] m, ref int c)
        {
            byte[] k = new byte[size];
            for (int i = 0; i < k.Length; i++) k[i] = m[i + c];
            c += k.Length;
            return (k);
        }

        public Array Redim(Array origArray, int desiredSize)
        {
            Type t = origArray.GetType().GetElementType();
            Array newArray = Array.CreateInstance(t, desiredSize);
            Array.Copy(origArray, 0, newArray, 0, Math.Min(origArray.Length, desiredSize));
            return (newArray);

        }
    }

    public class IXTRANS_PACK : UTIL_TYPES
    {
        public ushort TERM_ID;
        private ushort CAT_COUNT;
        private byte VER;
        private uint GUID_DB;
        private uint TRANS_SIZE;
        private byte[] Cats = null;

        private bool isWasBegin = false;
        public IXTRANS_PACK(ushort Term, byte Ver)
        {
            Cats = null;
            TERM_ID = Term;
            TRANS_SIZE = 0;
            VER = Ver;
            GUID_DB = 0;
            CAT_COUNT = 0;
            isWasBegin = true;
        }

        public byte[] GetBytes()
        {
            if (isWasBegin)
            {
                byte[] ret = null;

                GUID_DB = 0;
                TRANS_SIZE = 0;
                if (CAT_COUNT > 0) TRANS_SIZE = (uint)Cats.Length;
                isWasBegin = false;

                MemoryStream ms = new MemoryStream();
                set_ushort(TERM_ID, ms, ref ret);
                set_ushort(CAT_COUNT, ms, ref ret);
                set_byte(VER, ms, ref ret);
                set_uint(GUID_DB, ms, ref ret);
                set_uint(TRANS_SIZE, ms, ref ret);
                if (CAT_COUNT > 0)
                {
                    int oldsize = ms.ToArray().Length;
                    ret = (byte[])Redim(ms.ToArray(), ms.ToArray().Length + Cats.Length);
                    for (int i = 0; i < Cats.Length; i++)
                        ret[i + oldsize] = Cats[i];
                    return (ret);
                }
                else return (ms.ToArray());
            }
            else return (null);
        }

        public void AddCat(IXCAT cat)
        {
         //   byte[] ret = null;
            int oldsize = 0;

            byte[] data = cat.GetBytes();

            if (Cats == null) Cats = new byte[data.Length];
            else
            {
                oldsize = Cats.Length;
                Cats = (byte[])Redim(Cats, data.Length + oldsize);
            }
            for (int i = 0; i < data.Length; i++)
                Cats[i + oldsize] = data[i];
            CAT_COUNT++;
        }


    };
    public class IXTRANS_UNPACK : UTIL_TYPES
    {
        public ushort TERM_ID;
        public ushort CAT_COUNT;
        public byte VER;
        public uint GUID_DB;
        public uint TRANS_SIZE;
        private byte[] Cats = null;


        private int SetBytes(byte[] m)
        {
            int c = 0;

            TERM_ID = get_ushort(TERM_ID, m, ref c);
            CAT_COUNT = get_ushort(CAT_COUNT, m, ref c);
            VER = get_byte(VER, m, ref c);
            GUID_DB = get_uint(GUID_DB, m, ref c);
            TRANS_SIZE = get_uint(TRANS_SIZE, m, ref c);
            return c;
        }
        public IXTRANS_UNPACK(byte[] p)
        {
            int c = SetBytes(p);
            Cats = new byte[p.Length - c];
            for (int i = 0; i < p.Length - c; i++)
                Cats[i] = p[i + c];

            if ((Cats != null) && (Cats.Length > 0))
            {

                while (point < Cats.Length)
                {
                    if (ixcat == null)
                    {
                        ixcat = new IXCAT[1];
                    }
                    else
                    {
                        ixcat = (IXCAT[])Redim(ixcat, ixcat.Length + 1);
                    }
                    ixcat[ixcat.Length - 1] = new IXCAT();
                    point = ixcat[ixcat.Length - 1].SetBytes(Cats, point);
                }
            }
        }

        int point = 0;
        private IXCAT[] ixcat = null;
        private int curpack = 0;

        public IXCAT ReadNextCat()
        {
            if (ixcat != null)
                if ((ixcat.Length > 0) && (ixcat.Length >= curpack + 1))
                {
                    IXCAT t = new IXCAT();
                    t = ixcat[curpack];
                    curpack++;
                    return (t);
                }
            return null;
        }
    };

    public class Share
    {

        public static string mssql_connect = "server=db.corepay.local;Trusted_Connection=false;uid=platerra_arm;pwd=gkfnthhf_fhv;database=Corepay;Connect Timeout=60;max pool size=1000;Pooling=True";


        public class Log
        {
            static string date_correct(string date)
            {
                if (date.Length < 2)
                    return date.Insert(0, "0");
                else
                    if (date.Length > 2) // millisec
                        return date.Remove(2, date.Length - 2);
                return date;

            }
            public static void Write(string str)
            {
                string year = date_correct(DateTime.Now.Year.ToString().Remove(0, 2));
                string day = date_correct(DateTime.Now.Day.ToString());
                string month = date_correct(DateTime.Now.Month.ToString());
                string hour = date_correct(DateTime.Now.Hour.ToString());
                string min = date_correct(DateTime.Now.Minute.ToString());
                string sec = date_correct(DateTime.Now.Second.ToString());

                string date = hour + min + sec;
                string namefile = day + "_" + month + "_" + year + ".log";
                try
                {
                    if (!Directory.Exists("Log"))
                        Directory.CreateDirectory("Log");

                    StreamWriter sw = File.AppendText("Log\\" + namefile);
                    sw.WriteLine("---  " + date + "  -----------------------------------------------");
                    sw.WriteLine("\n");

                    sw.WriteLine(str);

                    sw.WriteLine("\n");
                    sw.WriteLine("----------------------------------------------------------------------");

                    sw.Flush();
                    sw.Close();
                }
                catch { }
            }

        }

        public static Array Redim(Array origArray, int desiredSize)
        {
            Type t = origArray.GetType().GetElementType();
            Array newArray = Array.CreateInstance(t, desiredSize);
            Array.Copy(origArray, 0, newArray, 0, Math.Min(origArray.Length, desiredSize));
            return (newArray);

        }


    }


    //упаковка категорий данных
    public class IXCAT : UTIL_TYPES
    {
        //данные протокола
        public ushort CAT = 0;
        public uint CAT_SIZE = 0;
        //переменные класса
        public byte[] Data = null;
        //методы класса
        public IXCAT(ushort Cat)
        {
            CAT = Cat;
        }
        public IXCAT() { }
        public byte[] GetBytes()
        {
            CAT_SIZE = 0;
            if ((Data != null) && (Data.Length > 0))
            {
                CAT_SIZE = (uint)Data.Length;
            }
            byte[] ret = null;
            MemoryStream ms = new MemoryStream();
            set_ushort(CAT, ms, ref ret);
            set_uint(CAT_SIZE, ms, ref ret);
            if (CAT_SIZE > 0)
            {
                int oldsize = ms.ToArray().Length;
                ret = (byte[])Redim(ms.ToArray(), ms.ToArray().Length + Data.Length);
                for (int i = 0; i < Data.Length; i++)
                    ret[i + oldsize] = Data[i];
                return (ret);
            }
            else return (ms.ToArray());
        }

        public void AddData(byte[] d)
        {
            int oldsize = 0;

            if (Data == null) Data = new byte[d.Length];
            else
            {
                oldsize = Data.Length;
                Data = (byte[])Redim(Data, d.Length + oldsize);
            }
            for (int i = 0; i < d.Length; i++)
                Data[i + oldsize] = d[i];
        }

        public int SetBytes(byte[] t, int k)
        {
            int c = 0;
            byte[] m = new byte[t.Length - k];
            for (int i = 0; i < t.Length - k; i++)
                m[i] = t[i + k];
            CAT = get_ushort(CAT, m, ref c);
            CAT_SIZE = get_uint(CAT_SIZE, m, ref c);
            if (Data == null) Data = new byte[CAT_SIZE];
            for (int i = 0; i < CAT_SIZE; i++)
                Data[i] = m[i + c];
            int p = Data.Length + c + k;
            return p;
        }
    };

    public class IXDATA 
    {
        public ushort id_term ;
        public uint id_db; 
        public byte ver ;

        public uint id_term_pack ;
        public ushort time_size ;
        public byte type_pack ;

        public uint id_term_record ;
        public ushort resource ;
        public byte type ;
        public byte[] data ;

    }

    public interface IPLUGIN_SERVER
    {
        string Application { get; }
        string Name { get; }
        int Version { get; }
        ushort Category { get; }

        bool ProcStart();
        bool ProcSend(int id_term, ref IXCAT ixcat);
        bool ProcRecieve(IXDATA ixdata, IXCAT ixcat);
        bool ProcError();
    }


    public class ServerPluginsManager
    {
        private Array Redim(Array origArray, int desiredSize)
        {
            Type t = origArray.GetType().GetElementType();
            Array newArray = Array.CreateInstance(t, desiredSize);
            Array.Copy(origArray, 0, newArray, 0, Math.Min(origArray.Length, desiredSize));
            return (newArray);

        }
        private class Plugin
        {
            public IPLUGIN_SERVER i_pack;
            public bool exclude = false;
        }


        public ServerPluginsManager(string appname)
        {
            this.appname = appname;
            FindPlugins();
        }

        private Plugin[] plugins = null;
        private string appname = "";

        private void ExcludeOldPlugins()
        {
            if ((plugins != null) && (plugins.Length > 0))
            {
                for (int k = 0; k < plugins.Length; k++)
                {
                    if (plugins[k].exclude == false)
                    {
                        string w = plugins[k].i_pack.Name;
                        for (int m = 0; m < plugins.Length; m++)
                        {
                            if (plugins[m].i_pack.Name == w)
                            {
                                if (plugins[m].i_pack.Version > plugins[k].i_pack.Version)
                                    plugins[m].exclude = true;
                            }
                        }
                    }
                }
            }
        }



        /*
        public bool FindPlugins()
        {
            plugins= null;
            AppDomain ad= AppDomain.CreateDomain("TempDomain");
			
            string folder = System.AppDomain.CurrentDomain.BaseDirectory+"bin/";
			
            //ad.SetDynamicBase(folder);

            string[] files = Directory.GetFiles(folder, "*.dll");
            foreach (string file in files)
                try
                {
					
                    Assembly assembly = ad.Load("ixengine");
					
                    foreach (Type type in assembly.GetTypes())
                    {
                        Type iface = type.GetInterface("IPLUGIN_SERVER");
                        if (iface != null)
                        {
                            IPLUGIN_SERVER ip= (IPLUGIN_SERVER)Activator.CreateInstance(type);
                            if(ip.Application==appname)
                            {
                                if(plugins==null)
                                {
                                    plugins= new Plugin[1];
                                }
                                else
                                {
                                    plugins= (Plugin[])Redim(plugins,plugins.Length+1);
                                }
                                plugins[plugins.Length-1]=new Plugin();
                                plugins[plugins.Length-1].i_pack= ip;
                            }
                        }
                    }
                }

                catch (Exception ex)
                {
                    int d=0;
                }
            ExcludeOldPlugins();
            AppDomain.Unload(ad);
			
            return false;
        }
*/

        public bool AddPlugin(IPLUGIN_SERVER ip)
        {
            if (plugins == null)
            {
                plugins = new Plugin[1];
            }
            else
            {
                plugins = (Plugin[])Redim(plugins, plugins.Length + 1);
            }
            plugins[plugins.Length - 1] = new Plugin();
            plugins[plugins.Length - 1].i_pack = ip;
            return true;
        }
        public bool FindPlugins()
        {
            plugins = null;


            try
            {
                AddPlugin(new SERVER_PACK_FBDB());
                AddPlugin(new SERVER_COM_SHORT());






            }
            catch //(Exception ex)
            {
                //int d = 0;
            }
            ExcludeOldPlugins();
            return false;
        }

        public bool PoolExec_ProcStart()
        {
            if ((plugins != null) && (plugins.Length > 0))
            {
                for (int k = 0; k < plugins.Length; k++)
                {
                    plugins[k].i_pack.ProcStart();
                }
                return true;
            }
            return false;
        }
        public bool PoolExec_ProcSend(ref IXTRANS_PACK t)
        {
            if ((plugins != null) && (plugins.Length > 0))
            {
                for (int k = 0; k < plugins.Length; k++)
                {
                    IXCAT ixcat = new IXCAT(plugins[k].i_pack.Category);
                    plugins[k].i_pack.ProcSend(t.TERM_ID, ref ixcat);
                    t.AddCat(ixcat);
                }
                return true;
            }
            return false;
        }



        public int IX_ADD_CATEGORY(int ID_DB_TRANS, IXCAT cat)
        {
            string com = "";
            int ID_DB_CAT = 0;
            SqlDataReader d = null;
            SqlConnection myConnection = null;
            SqlCommand myCommand = null;

            try
            {
                com = "EXEC IX_ADD_CATEGORY #id_trans#,#category#;";
                com = com.Replace("#id_trans#", ID_DB_TRANS.ToString());
                com = com.Replace("#category#", cat.CAT.ToString());

                myConnection = new SqlConnection(Share.mssql_connect);
                myCommand = new SqlCommand(com, myConnection);
                myConnection.Open();
                d = myCommand.ExecuteReader();
                if (d.Read()) ID_DB_CAT = int.Parse(d.GetValue(0).ToString());
                d.Close();
                myConnection.Close();
                return ID_DB_CAT;
            }
            catch
            {
                if (myConnection != null)
                    myConnection.Close();
                return (0);
            }
        }


        public bool PoolExec_ProcRecieve( IXTRANS_UNPACK t)
        {
            if ((plugins != null) && (plugins.Length > 0))
            {
                IXCAT ixcat = t.ReadNextCat();
                IXDATA ixdata = new IXDATA();
                ixdata.id_term = t.TERM_ID;
                ixdata.id_db = t.GUID_DB;
                ixdata.ver = t.VER;
                while (ixcat != null)
                {
                    for (int k = 0; k < plugins.Length; k++)
                    {
                        if (plugins[k].i_pack.Category == ixcat.CAT)
                        {
                            //int ID_DB_CAT= IX_ADD_CATEGORY(ID_DB_TRANS,ixcat);
                            //if(ID_DB_CAT>0)
                            //	plugins[k].i_pack.ProcRecieve(ID_DB_CAT,ixcat);		
                            //else return false  ;
                            plugins[k].i_pack.ProcRecieve(ixdata, ixcat);


                        }
                    }
                    ixcat = t.ReadNextCat();
                }
                return true;
            }
            return false;
        }
        public bool PoolExec_ProcError(ref IXTRANS_PACK t)
        {
            if ((plugins != null) && (plugins.Length > 0))
            {
                for (int k = 0; k < plugins.Length; k++)
                {
                    plugins[k].i_pack.ProcError();
                }
                return true;
            }
            return false;
        }
    }


    //--------------------------------------------------------------------
    //--------------------------------------------------------------------









}
