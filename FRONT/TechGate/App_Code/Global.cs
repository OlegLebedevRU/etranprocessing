// Decompiled with JetBrains decompiler
// Type: TechGate.Global
// Assembly: TechGate, Version=1.0.4724.32104, Culture=neutral, PublicKeyToken=null
// MVID: 7D25CA8F-6EB1-4AE3-AFB7-31AA5758287A
// Assembly location: C:\PlaterraSvn\PROCESSING\FRONT\TechGate\bin\TechGate.dll

using System;
using System.Web;

namespace TechGate
{
  public class Global : HttpApplication
  {
    protected void Application_Start(object sender, EventArgs e)
    {
      GlobalObjectsManager.Init();
      GlobalObjectsManager.Logger.Info((object) "Приложение запущено.");
    }

    protected void Application_End(object sender, EventArgs e)
    {
      GlobalObjectsManager.Logger.Info((object) "Приложение завершается.");
    }
  }
}
