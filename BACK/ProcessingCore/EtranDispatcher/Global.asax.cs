using System;
using System.Data;
using System.Configuration;
using System.Collections;
using System.Web;
using System.Web.Security;
using System.Web.SessionState;

namespace EtranDispatcher
{
    public class Global : System.Web.HttpApplication
    {


        protected void Application_Start(object sender, EventArgs e)
        {
            GlobalObjectsManager.Init();
            GlobalObjectsManager.Logger.Debug("Приложение запущено.");

        }
        protected void Application_Error(Object sender, EventArgs e)
        {
            Exception ex = Server.GetLastError();

            if (ex != null)
            {
                GlobalObjectsManager.Logger.Error("При обращении к службе возникло исключение.", ex);
                Server.ClearError();
            }
        }



        protected void Application_End(object sender, EventArgs e)
        {
            GlobalObjectsManager.Logger.Debug("Приложение завершается.");
        }
    }
}