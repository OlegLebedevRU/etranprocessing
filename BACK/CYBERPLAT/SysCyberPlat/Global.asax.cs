using System;
using System.Collections;
using System.Configuration;
using System.Data;
using System.Web;
using System.Web.Security;
using System.Web.SessionState;

namespace SysCyberPlat
{
    public class Global : System.Web.HttpApplication
    {

        protected void Application_Start(object sender, EventArgs e)
        {
            GlobalObjectsManager.Init();
            GlobalObjectsManager.Logger.Info("Приложение запущено.");

        }

        protected void Application_End(object sender, EventArgs e)
        {
            GlobalObjectsManager.Logger.Info("Приложение завершается.");
        }
    }
}