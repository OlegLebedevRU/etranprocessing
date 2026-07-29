using System;
using System.Configuration;
using System.Data;
using System.Web;
using System.Web.Security;
using System.Web.UI;
using System.Web.UI.HtmlControls;
using System.Web.UI.WebControls;
using System.Web.UI.WebControls.WebParts;

public partial class _Default : System.Web.UI.Page 
{
    static Guid m_guid = Guid.NewGuid();
    protected void Page_Load(object sender, EventArgs e)
    {

        try
        {
            GlobalObjectsManager.Logger.Info(m_guid.ToString() + " * " + Request.RawUrl);
            if (Request.HttpMethod.ToUpper() == "POST")
            {
                ShowRequestData();
            }
            else
                if (Request.HttpMethod.ToUpper() == "GET")
                {
                    //GlobalObjectsManager.Logger.Error(Request);
                }
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error(ex);
        }
    }


    public void ShowRequestData()
    {
        System.IO.Stream body = Request.InputStream;
        System.Text.Encoding encoding = Request.ContentEncoding;
        System.IO.StreamReader reader = new System.IO.StreamReader(body, encoding);
        if (Request.ContentType != null)
        {
            //Console.WriteLine("Client data content type {0}", request.ContentType);
            GlobalObjectsManager.Logger.Info(m_guid.ToString() + " * " + "Client data content type " + Request.ContentType);
        }
        //Console.WriteLine("Client data content length {0}", request.ContentLength64);
        GlobalObjectsManager.Logger.Info(m_guid.ToString() + " * " + "Client data content length " + Request.ContentLength);

        //Console.WriteLine("Start of client data:");
        GlobalObjectsManager.Logger.Info(m_guid.ToString() + " * " + "Start of client data:");
        // Convert the data to a string and display it on the console.
        string s = reader.ReadToEnd();
        //Console.WriteLine(s);
        //Console.WriteLine("End of client data:");
        GlobalObjectsManager.Logger.Info(m_guid.ToString() + " * " + s);
        body.Close();
        reader.Close();
        // If you are finished with the request, it should be closed also.
    }


}
