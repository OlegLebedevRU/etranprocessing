using System;
using System.IO;
using System.Text;
using System.Runtime.InteropServices;
using CPX_SERVER;
using CPX;
using System.Data;
using System.Data.SqlClient;
namespace CPX_IXENGINE
{
    //----------------------------------------------------------------
    // Общие классы для распаковки пакетов категории данных 0x01      
    //----------------------------------------------------------------
    public class SERVER_PACK_FBDB : IPLUGIN_SERVER
    {
        //информационные данные интерфейса----------------------------
        public string Application
        {
            get { return "ixEngine"; }
        }
        public string Name
        {
            get { return "SERVER_PACK_FBDB"; }
        }
        public int Version
        {
            get { return 1; }
        }
        public ushort Category
        {
            get { return 1; }
        }

        //реализация методов интерфейса-------------------------------
        public bool ProcStart()
        {
            return false;
        }
        public bool ProcSend(int id_term, ref IXCAT ixcat)
        {
            IXENGINE_DB_PACK p = new IXENGINE_DB_PACK(1, 0);

            if ((Wrote != null) && (Wrote.Length > 0))
            {
                for (int i = 0; i < Wrote.Length; i++)
                {
                    IXENGINE_DB_RECORD r = new IXENGINE_DB_RECORD(Wrote[i]);
                    p.AddRecord(r);
                }
            }
            ixcat.AddData(p.GetBytes());
            return true;
        }

        public bool ProcRecieve(IXDATA ixdata, IXCAT ixcat)
        {
            int exec = 0;
            UNPACK_CAT1 packs = new UNPACK_CAT1(ixcat.Data);
            IXPACK p = packs.ReadNextPack();
            while (p != null)
            {
               // int ID_DB_PACK = IX_CAT1_ADD_PACKAGE(ID_DB_CAT, p);
                //if (ID_DB_PACK > 0)
                //{

                ixdata.id_term_pack = p.ID_PACK;
                ixdata.time_size = p.TIME_SIZE;
                ixdata.type_pack = p.TYPE_PACK;
                
                IXRECORD r = p.ReadNextRecord();
                
                while (r != null)
                {
                    ixdata.id_term_record = r.ID_EVENT;
                    ixdata.resource = r.TYPE_RES;
                    ixdata.type = r.TYPE_EVENT;
                    ixdata.data = r.DATA;

                    if (ixdata.type_pack == 0) exec = 1;
                    exec= IX_PROCESS_RECORD(ixdata);

                    r = p.ReadNextRecord();
                }
                    if (Wrote == null) Wrote = new uint[1];
                    else
                    {
                        Wrote = (uint[])Share.Redim(Wrote, Wrote.Length + 1);
                    }
                    Wrote[Wrote.Length - 1] = p.ID_PACK;
                //}
                p = packs.ReadNextPack();
            }
            /*
            ixdata.id_term_pack = 0;
            ixdata.time_size = 0;
            ixdata.type_pack = 0;
            ixdata.id_term_record = 0;
            ixdata.resource = 131;
            ixdata.type = 0;
            if (exec == 0)
            {
                ixdata.data = (new System.Text.UTF8Encoding()).GetBytes("200");//ошибка бд
            }
            //else ixdata.data = (new System.Text.UTF8Encoding()).GetBytes("0");
            IX_PROCESS_RECORD(ixdata);
            */
            return true;
        }

        public bool ProcError()
        {
            return true;
        }

        //переменные класса
        private uint[] Wrote = null;
        //методы класса

        public int IX_PROCESS_RECORD(IXDATA ixdata)
        {
            string com = "";

            SqlConnection myConnection = null;
            SqlCommand myCommand = null;
            try
            {
                com = "EXEC IX_PROCESS_RECORD @id_term, @id_db,@ver,@id_term_pack,@time_size,@type_pack,@id_term_record,@resource,@type,@data;";
                myConnection = new SqlConnection(Share.mssql_connect);
                myCommand = new SqlCommand(com, myConnection);
                //1
                SqlParameter param = new SqlParameter("@id_term", SqlDbType.Int);
                param.Value = ixdata.id_term;
                myCommand.Parameters.Add(param);
                //2
                param = new SqlParameter("@id_db", SqlDbType.Int);
                param.Value = ixdata.id_db;
                myCommand.Parameters.Add(param);
                //3
                param = new SqlParameter("@ver", SqlDbType.Int);
                param.Value = ixdata.ver;
                myCommand.Parameters.Add(param);
                //4
                param = new SqlParameter("@id_term_pack", SqlDbType.Int);
                param.Value = ixdata.id_term_pack;
                myCommand.Parameters.Add(param);
                //5
                param = new SqlParameter("@time_size", SqlDbType.Int);
                param.Value = ixdata.time_size;
                myCommand.Parameters.Add(param);
                //6
                param = new SqlParameter("@type_pack", SqlDbType.Int);
                param.Value = ixdata.type_pack;
                myCommand.Parameters.Add(param);
                //7
                param = new SqlParameter("@id_term_record", SqlDbType.Int);
                param.Value = ixdata.id_term_record;
                myCommand.Parameters.Add(param);
                //8
                param = new SqlParameter("@resource", SqlDbType.Int);
                param.Value = ixdata.resource;
                myCommand.Parameters.Add(param);
                //9
                param = new SqlParameter("@type", SqlDbType.Int);
                param.Value = ixdata.type;
                myCommand.Parameters.Add(param);
                //10
                param = new SqlParameter("@data", SqlDbType.VarChar);
                param.Value = System.Text.Encoding.UTF8.GetString(ixdata.data);
                myCommand.Parameters.Add(param);


                myConnection.Open();
                myCommand.ExecuteNonQuery();
                myConnection.Close();
                return 1;
            }
            catch //(Exception e)
            {
                if (myConnection != null)
                    myConnection.Close();
                return (0);
            }
        }


        /*
        public int IX_CAT1_ADD_RECORD(int ID_DB_PACK, IXRECORD ev)
        {
            string com = "";

            SqlConnection myConnection = null;
            SqlCommand myCommand = null;
            try
            {
                com = "EXEC IX_CAT1_ADD_RECORD #id_pack#,#id_term_record#,#resource#,@data,#type#;";
                com = com.Replace("#id_pack#", ID_DB_PACK.ToString());
                com = com.Replace("#id_term_record#", ev.ID_EVENT.ToString());
                com = com.Replace("#resource#", ev.TYPE_RES.ToString());
                com = com.Replace("#type#", ev.TYPE_EVENT.ToString());
                myConnection = new SqlConnection(Share.mssql_connect);
                myCommand = new SqlCommand(com, myConnection);
                SqlParameter param = new SqlParameter("@data", SqlDbType.VarChar);
                param.Value = System.Text.Encoding.UTF8.GetString(ev.DATA);
                myCommand.Parameters.Add(param);
                myConnection.Open();
                myCommand.ExecuteNonQuery();
                myConnection.Close();
                return 1;
            }
            catch //(Exception e)
            {
                if (myConnection != null)
                    myConnection.Close();
                return (0);
            }
        }

        public int IX_CAT1_ADD_PACKAGE(int ID_DB_CAT, IXPACK pk)
        {
            string com = "";
            int ID_DB_PACK = 0;
            SqlDataReader d = null;
            SqlConnection myConnection = null;
            SqlCommand myCommand = null;

            try
            {
                com = "EXEC IX_CAT1_ADD_PACKAGE #id_cat#,#id_term_pack#,#time_size#,#type#;";
                com = com.Replace("#id_cat#", ID_DB_CAT.ToString());
                com = com.Replace("#id_term_pack#", pk.ID_PACK.ToString());
                com = com.Replace("#time_size#", pk.TIME_SIZE.ToString());
                com = com.Replace("#type#", pk.TYPE_PACK.ToString());
                myConnection = new SqlConnection(Share.mssql_connect);
                myCommand = new SqlCommand(com, myConnection);
                myConnection.Open();
                d = myCommand.ExecuteReader();
                if (d.Read()) ID_DB_PACK = int.Parse(d.GetValue(0).ToString());
                d.Close();
                myConnection.Close();
                return ID_DB_PACK;
            }
            catch
            {
                if (myConnection != null)
                    myConnection.Close();
                return (0);
            }
        }
        */
        //классы -----------------------------------------------------


        class IXENGINE_DB_RECORD : UTIL_TYPES
        {
            private uint ID_RECORD;

            public IXENGINE_DB_RECORD(uint ID_Record)
            {
                ID_RECORD = ID_Record;
            }
            public byte[] GetBytes()
            {
                byte[] ret = null;

                MemoryStream ms = new MemoryStream();
                set_uint(ID_RECORD, ms, ref ret);
                return (ms.ToArray());
            }
        }

        class IXENGINE_DB_PACK : UTIL_TYPES
        {
            public uint ID_PACK;
            public byte TYPE_PACK;
            public ushort RECORDS_COUNT;
            public uint PACK_SIZE;
            public byte[] Records;

            private bool isWasBegin = false;
            public IXENGINE_DB_PACK(uint ID_Pack, byte Type_Pack)
            {
                Records = null;

                ID_PACK = ID_Pack;
                TYPE_PACK = Type_Pack;
                RECORDS_COUNT = 0;
                PACK_SIZE = 0;
                isWasBegin = true;
            }
            public byte[] GetBytes()
            {
                if (isWasBegin)
                {
                    if (Records == null) PACK_SIZE = 0;
                    else PACK_SIZE = (uint)Records.Length;
                    isWasBegin = false;

                    byte[] ret = null;

                    MemoryStream ms = new MemoryStream();
                    set_uint(ID_PACK, ms, ref ret);
                    set_byte(TYPE_PACK, ms, ref ret);
                    set_ushort(RECORDS_COUNT, ms, ref ret);
                    set_uint(PACK_SIZE, ms, ref ret);
                    if (Records != null)
                    {
                        int oldsize = ms.ToArray().Length;
                        ret = (byte[])Redim(ms.ToArray(), ms.ToArray().Length + Records.Length);
                        for (int i = 0; i < Records.Length; i++)
                            ret[i + oldsize] = Records[i];
                        return (ret);
                    }
                    return (ms.ToArray());
                }
                else return (null);
            }
            public void AddRecord(IXENGINE_DB_RECORD e)
            {
                int oldsize = 0;
                byte[] ev = e.GetBytes();
                if (Records == null) Records = new byte[ev.Length];
                else
                {
                    oldsize = Records.Length;
                    Records = (byte[])Redim(Records, ev.Length + oldsize);
                }
                for (int i = 0; i < ev.Length; i++)
                    Records[i + oldsize] = ev[i];
                RECORDS_COUNT++;
            }
        }

        public class IXRECORD : UTIL_TYPES
        {
            public uint ID_EVENT;
            public ushort TYPE_RES;
            public byte TYPE_EVENT;
            public ushort DATA_SIZE;
            public byte[] DATA;

            public int SetBytes(byte[] t, int k)
            {
                int c = 0;

                byte[] m = new byte[t.Length - k];
                for (int i = 0; i < t.Length - k; i++)
                    m[i] = t[i + k];

                ID_EVENT = get_uint(ID_EVENT, m, ref c);
                TYPE_RES = get_ushort(TYPE_RES, m, ref c);
                TYPE_EVENT = get_byte(TYPE_EVENT, m, ref c);
                DATA_SIZE = get_ushort(DATA_SIZE, m, ref c);

                DATA = new byte[DATA_SIZE];
                for (int i = 0; i < DATA_SIZE; i++)
                    DATA[i] = m[i + c];
                string a = (new System.Text.UTF8Encoding()).GetString(DATA);
                c += DATA_SIZE + k;
                return c;
            }

        }

        public class IXPACK : UTIL_TYPES
        {
            public uint ID_PACK;
            public byte TYPE_PACK;
            public ushort TIME_SIZE;
            public ushort RECORDS_COUNT;
            public uint PACK_SIZE;

            public byte[] Data;
            public int SetBytes(byte[] t, int k)
            {
                int c = 0;

                byte[] m = new byte[t.Length - k];
                for (int i = 0; i < t.Length - k; i++)
                    m[i] = t[i + k];

                ID_PACK = get_ushort(ID_PACK, m, ref c);
                TYPE_PACK = get_byte(TYPE_PACK, m, ref c);
                TIME_SIZE = get_ushort(TIME_SIZE, m, ref c);
                RECORDS_COUNT = get_ushort(RECORDS_COUNT, m, ref c);
                PACK_SIZE = get_uint(PACK_SIZE, m, ref c);

                if (Data == null) Data = new byte[PACK_SIZE];

                for (int i = 0; i < PACK_SIZE; i++)
                    Data[i] = m[i + c];
                c += Data.Length + k;

                if ((Data != null) && (Data.Length > 0))
                {
                    while (point < Data.Length)
                    {
                        if (ixrecord == null)
                        {
                            ixrecord = new IXRECORD[1];
                        }
                        else
                        {
                            ixrecord = (IXRECORD[])Redim(ixrecord, ixrecord.Length + 1);
                        }
                        ixrecord[ixrecord.Length - 1] = new IXRECORD();
                        point = ixrecord[ixrecord.Length - 1].SetBytes(Data, point);
                    }

                }
                return c;
            }

            private IXRECORD[] ixrecord = null;
            private int point = 0;
            private int curpack = 0;

            public IXPACK()
            {
            }

            public IXRECORD ReadNextRecord()
            {
                if (ixrecord != null)
                    if ((ixrecord.Length > 0) && (ixrecord.Length >= curpack + 1))
                    {
                        IXRECORD t = new IXRECORD();
                        t = ixrecord[curpack];
                        curpack++;
                        return (t);
                    }
                return null;
            }
        }
        public class UNPACK_CAT1 : UTIL_TYPES
        {
            public byte[] Data;
            private IXPACK[] ixpack = null;
            private int point = 0;
            private int curpack = 0;

            public UNPACK_CAT1(byte[] p)
            {
                if ((p != null) && (p.Length > 0))
                {
                    Data = new byte[p.Length];
                    for (int i = 0; i < p.Length; i++)
                        Data[i] = p[i];

                    while (point < Data.Length)
                    {
                        if (ixpack == null)
                        {
                            ixpack = new IXPACK[1];
                        }
                        else
                        {
                            ixpack = (IXPACK[])Redim(ixpack, ixpack.Length + 1);
                        }
                        ixpack[ixpack.Length - 1] = new IXPACK();
                        point = ixpack[ixpack.Length - 1].SetBytes(Data, point);
                    }
                }
            }

            public IXPACK ReadNextPack()
            {
                if (ixpack != null)
                    if ((ixpack.Length > 0) && (ixpack.Length >= curpack + 1))
                    {
                        IXPACK t = new IXPACK();
                        t = ixpack[curpack];
                        curpack++;
                        return (t);
                    }
                return null;
            }
        }
    }
    public class SERVER_COM_SHORT : IPLUGIN_SERVER
    {
        //информационные данные интерфейса----------------------------
        public string Application
        {
            get { return "ixEngine"; }
        }
        public string Name
        {
            get { return "SERVER_COM_SHORT"; }
        }
        public int Version
        {
            get { return 1; }
        }
        public ushort Category
        {
            get { return 2; }
        }

        //реализация методов интерфейса-------------------------------
        public bool ProcStart()
        {
            return false;
        }
        public bool ProcSend(int id_term, ref IXCAT ixcat)
        {
            COM_PACK p = new COM_PACK();

            string com = "";
            SqlDataReader d = null;
            SqlConnection myConnection = null;
            SqlCommand myCommand = null;
            try
            {
                com = "EXEC IX_CAT2_GET_COMMANDS " + id_term.ToString() + ";";
                myConnection = new SqlConnection(Share.mssql_connect);
                myCommand = new SqlCommand(com, myConnection);
                myConnection.Open();
                d = myCommand.ExecuteReader();
                while (d.Read())
                {
                    string text = "";
                    if (!d.IsDBNull(2))
                        text = d.GetValue(2).ToString();
                    COM_RECORD r = new COM_RECORD((uint)d.GetInt32(0), (ushort)d.GetInt32(1), text);
                    p.AddCom(r);
                }
                d.Close();
                myConnection.Close();
                ixcat.AddData(p.GetBytes());
            }
            catch
            {
                if (myConnection != null)
                    myConnection.Close();
            }
            return true;
        }

        public bool ProcRecieve(IXDATA ixdata, IXCAT ixcat)
        {
            return true;
        }

        public bool ProcError()
        {
            return true;
        }

        //переменные класса
        //классы -----------------------------------------------------
        private Array Redim(Array origArray, int desiredSize)
        {
            Type t = origArray.GetType().GetElementType();
            Array newArray = Array.CreateInstance(t, desiredSize);
            Array.Copy(origArray, 0, newArray, 0, Math.Min(origArray.Length, desiredSize));
            return (newArray);
        }


        class COM_RECORD : UTIL_TYPES
        {
            private uint ID_COM;
            private ushort ID_RES;
            private ushort DATA_SIZE;
            private string DATA = "";

            public COM_RECORD(uint ID_Com, ushort Id_Res, string Data)
            {
                ID_COM = ID_Com;
                ID_RES = Id_Res;
                DATA_SIZE = 0;
                DATA = Data;
            }
            public byte[] GetBytes()
            {
                byte[] ret = null;
                try
                {
                    byte[] Data_String = Encoding.UTF8.GetBytes(DATA);
                    DATA_SIZE = (ushort)Data_String.Length;
                    MemoryStream ms = new MemoryStream();
                    set_uint(ID_COM, ms, ref ret);
                    set_ushort(ID_RES, ms, ref ret);
                    set_ushort(DATA_SIZE, ms, ref ret);
                    int oldsize = ms.ToArray().Length;
                    ret = (byte[])Redim(ms.ToArray(), ms.ToArray().Length + DATA_SIZE);
                    for (int i = 0; i < Data_String.Length; i++)
                        ret[i + oldsize] = Data_String[i];
                }
                catch (Exception e)
                {
                    //Share.Log.Write("025: " + e.Message);
                }
                return (ret);
            }
        }


        class COM_PACK : UTIL_TYPES
        {
            public ushort COM_COUNT;
            public uint PACK_SIZE;
            public byte[] Coms;

            private bool isWasBegin = false;
            public COM_PACK()
            {
                Coms = null;
                COM_COUNT = 0;
                PACK_SIZE = 0;
                isWasBegin = true;
            }
            public byte[] GetBytes()
            {
                try
                {
                    if (isWasBegin)
                    {
                        if (Coms == null) PACK_SIZE = 0;
                        else PACK_SIZE = (uint)Coms.Length;
                        isWasBegin = false;

                        byte[] ret = null;

                        MemoryStream ms = new MemoryStream();
                        set_ushort(COM_COUNT, ms, ref ret);
                        set_uint(PACK_SIZE, ms, ref ret);
                        if (Coms != null)
                        {
                            int oldsize = ms.ToArray().Length;
                            ret = (byte[])Redim(ms.ToArray(), ms.ToArray().Length + Coms.Length);
                            for (int i = 0; i < Coms.Length; i++)
                                ret[i + oldsize] = Coms[i];
                            return (ret);
                        }
                        return (ms.ToArray());
                    }
                    else return (null);
                }
                catch (Exception e)
                {
                    //Share.Log.Write("09: " + e.Message);
                }
                return (null);
            }
            public void AddCom(COM_RECORD e)
            {
                try
                {
                    int oldsize = 0;
                    byte[] ev = e.GetBytes();

                    if (Coms == null) Coms = new byte[ev.Length];
                    else
                    {
                        oldsize = Coms.Length;
                        Coms = (byte[])Redim(Coms, ev.Length + oldsize);
                    }

                    for (int i = 0; i < ev.Length; i++)
                        Coms[i + oldsize] = ev[i];

                    COM_COUNT++;
                }
                catch (Exception ex)
                {
                   // Share.Log.Write("026: " + ex.Message);
                }
            }
        }
    }
}
