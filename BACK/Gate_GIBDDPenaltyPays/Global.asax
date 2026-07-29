<%@ Application Language="C#" %>

<script runat="server">

    protected void Application_Start(object sender, EventArgs e)
    {
        GlobalObjectsManager.Init();
        GlobalObjectsManager.Logger.Info("Приложение запущено.");
    }

    protected void Application_End(object sender, EventArgs e)
    {
        GlobalObjectsManager.Logger.Info("Приложение завершается.");
    }
    
       
</script>
