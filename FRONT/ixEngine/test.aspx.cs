using System;
using System.Data;
using System.Data.SqlClient;
using System.Configuration;
using System.Collections;
using System.Web;
using System.Web.Security;
using System.Web.UI;
using System.Web.UI.WebControls;
using System.Web.UI.WebControls.WebParts;
using System.Web.UI.HtmlControls;
using CPX_SERVER;
public partial class test : System.Web.UI.Page
{
    protected void Page_Load(object sender, EventArgs e)
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
            param.Value = "9012";
            myCommand.Parameters.Add(param);
            //2
            param = new SqlParameter("@id_db", SqlDbType.Int);
            param.Value = 9;
            myCommand.Parameters.Add(param);
            //3
            param = new SqlParameter("@ver", SqlDbType.Int);
            param.Value = 10;
            myCommand.Parameters.Add(param);
            //4
            param = new SqlParameter("@id_term_pack", SqlDbType.Int);
            param.Value = 0;
            myCommand.Parameters.Add(param);
            //5
            param = new SqlParameter("@time_size", SqlDbType.Int);
            param.Value = 0;
            myCommand.Parameters.Add(param);
            //6
            param = new SqlParameter("@type_pack", SqlDbType.Int);
            param.Value = 0;
            myCommand.Parameters.Add(param);
            //7
            param = new SqlParameter("@id_term_record", SqlDbType.Int);
            param.Value = 0;
            myCommand.Parameters.Add(param);
            //8
            param = new SqlParameter("@resource", SqlDbType.Int);
            param.Value = 111;
            myCommand.Parameters.Add(param);
            //9
            param = new SqlParameter("@type", SqlDbType.Int);
            param.Value = 0;
            myCommand.Parameters.Add(param);
            //10
            param = new SqlParameter("@data", SqlDbType.VarChar);
            param.Value = "";//System.Text.Encoding.UTF8.GetString(ixdata.data);
            myCommand.Parameters.Add(param);


            myConnection.Open();
            myCommand.ExecuteNonQuery();
            myConnection.Close();
            Label1.Text = "OK";
        }
        catch (Exception l)
        {
            if (myConnection != null)
                myConnection.Close();
            Label1.Text = l.Message;
        }




    }
}
