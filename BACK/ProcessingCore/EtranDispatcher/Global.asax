<%@ Application Language="C#" %>

<script runat="server">

    void Application_Start(object sender, EventArgs e)
    {
        EtranDispatcher.GlobalObjectsManager.Init();
        EtranDispatcher.GlobalObjectsManager.Logger.Debug("Приложение запущено.");
    }

    void Application_Error(object sender, EventArgs e)
    {
        Exception ex = Server.GetLastError();
        if (ex != null)
        {
            EtranDispatcher.GlobalObjectsManager.Logger.Error("При обращении к службе возникло исключение.", ex);
            Server.ClearError();
        }
    }

    void Application_End(object sender, EventArgs e)
    {
        EtranDispatcher.GlobalObjectsManager.Logger.Debug("Приложение завершается.");
    }

</script>
