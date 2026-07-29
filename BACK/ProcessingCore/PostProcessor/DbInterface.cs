using System;
using System.Collections;
using System.Data;
using EtranLib.Data;

namespace PostProcessor
{
    /// <summary>
    /// Класс, инкапсулирующий логику работы с БД службы.
    /// </summary>
    public class DbInterface
    {

        static public DataSet ExecuteSP(int mp_id, int timeout)
        {
            DBManager dbs = new DBManager(EtranConfigurationManager.DbConnectionString);
            return (DataSet)dbs.Execute(EtranConfigurationManager.SP_NAME, CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null, mp_id);
        }

    }
}
