using System;
using System.Data;
using System.Configuration;
using System.Web;
using System.Web.Security;
using System.Web.UI;
using System.Web.UI.HtmlControls;
using System.Web.UI.WebControls;
using System.Web.UI.WebControls.WebParts;
using EtranLib.Data;


/// <summary>
/// Сводное описание для DBInterface
/// </summary>
public class DBInterface
{
    static public DataSet Autho(string pin, string sign)
    {
        bool NewSoft = (sign != null) ? true : false;
        DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
        string sp_name = "certman_front_auth3";
        if (NewSoft)
            sp_name = "certman_front_auth_new";
        GlobalObjectsManager.Logger.Info("EXEC SP: " + sp_name);
        return (DataSet)db.Execute(sp_name, CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null, pin);
    }

    static public DataSet UserAutho(string login, string pwd, string pin)
    {
        DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
        return (DataSet)db.Execute("certman_user_auth", CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null, login, pwd, pin);
    }

    static public DataSet Setup(string pin, string serial_number, string cpserial, string ca_serial)
    {
        DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
        return (DataSet)db.Execute("certman_front_setup", CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null, pin, serial_number, cpserial, ca_serial);
    }

    static public DataSet UserSetup(string login, string pwd, string pin, string serial_number)
    {
        DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
        return (DataSet)db.Execute("certman_user_setup", CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null, login, pwd, pin, serial_number);
    }
}
