using System;
using System.Collections;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Web;
using System.Web.SessionState;
using System.Web.UI;
using System.Web.UI.WebControls;
using System.Web.UI.HtmlControls;
using System.IO;
using CPX;
using System.Data.SqlClient;
using CPX_SERVER;

    /// <summary>
    /// Summary description for WebForm1.
    /// </summary>
    public partial class gate : System.Web.UI.Page
    {
        private void Page_Load(object sender, System.EventArgs e)
        {
            try
            {
               // int sernum = 0;
              //  string sn = "";

                /*

                sn = Context.Request.ClientCertificate.SerialNumber;
                sn = sn.Remove(0, sn.Length - 11).Replace("-", "");
                sernum = int.Parse(sn, System.Globalization.NumberStyles.HexNumber);

                CsGlobal.Logger.Info("Сертификат: " + sernum.ToString());
                */



               // if (CheckSerial(sernum) == 1)
               // {
                    CsGlobal.Logger.Info("Обмен информацией");

                    byte[] in_data = null;
                    byte[] out_data = null;
                    if (!ReceiveRequest(ref in_data)) return;
                    IXENGINE s = new IXENGINE();

                    s.Main(in_data, ref out_data);

                    SendResponse(out_data);

               // }
              //  else
             //   {
              //      CsGlobal.Logger.Info("Сертификат не валидный");
             //   }
                
            }
            catch(Exception ex) 
            {
                CsGlobal.Logger.Error("Исключение", ex);
            }
        }

        public static int CheckSerial(int sn)
        {
            int ret = 1;
        
            SqlConnection myConnection = null;
            SqlCommand myCommand = null;
            SqlDataReader r = null;
            try
            {
                myConnection = new SqlConnection(Share.mssql_connect);
                myConnection.Open();
                string com = "ChkSerialNumber";
                myCommand = new SqlCommand(com, myConnection);
                myCommand.CommandType = CommandType.StoredProcedure;
                myCommand.Parameters.Add("@sn", SqlDbType.Int);
                myCommand.Parameters["@sn"].Value = sn;
                r = myCommand.ExecuteReader();
                if (r.Read())
                {
                    ret = int.Parse(r.GetValue(0).ToString());
                   
                }
                r.Close();
                myConnection.Close();
            }
            catch (Exception ex)
            {
                ret = 1;
                //GlobalObjectsManager.Logger.Info(ex);
            }

            return ret;
        }




        public bool ReceiveRequest(ref byte[] in_data)
        {
            try
            {
                HttpRequest Request = HttpContext.Current.Request;
                long len = Request.InputStream.Length;
                byte[] input = new byte[len];
                Request.InputStream.Read(input, 0, Convert.ToInt32(len));
                Request.InputStream.Seek(0, SeekOrigin.Begin);
                //in_data= System.Text.Encoding.UTF8.GetString(input);
                in_data = input;
            }
            catch (Exception e3)
            {
                string err = e3.Message;
                return false;
            }
            return true;
        }

        public void Log(string Message) 
        {
            using (StreamWriter stream = new StreamWriter("C:\\LOG\\ixEngine.txt", true)) 
            {
                stream.WriteLine(Message);
                stream.Close();
                stream.Dispose();
            }
        }


        public bool SendResponse(byte[] out_data)
        {
            try
            {
                /*
                Response.Clear();
                Response.ClearContent();
                Response.ClearHeaders();
                Response.ContentType="application/text";
                Response.Buffer = true;
                Response.Write(out_data);
                Response.Flush();
                Response.End();
                */

                Response.Clear();
                Response.ClearContent();
                Response.ClearHeaders();
                Response.ContentType = "application/bin";
                Response.Buffer = true;
                Response.BinaryWrite(out_data);
                Response.Flush();
                Response.End();

            }
            catch //(Exception e4)
            {

                return false;
            }
            return true;
        }

        #region Web Form Designer generated code
        override protected void OnInit(EventArgs e)
        {
            //
            // CODEGEN: This call is required by the ASP.NET Web Form Designer.
            //
            InitializeComponent();
            base.OnInit(e);
        }

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.Load += new System.EventHandler(this.Page_Load);
        }
        #endregion
    }
