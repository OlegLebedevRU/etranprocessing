using System.ComponentModel;
using System.Configuration.Install;
using System.ServiceProcess;


namespace PingService
{
    [RunInstaller(true)]
    public partial class MyServiceInstaller : Installer
    {
        ServiceProcessInstaller serviceProcessInstaller =
                                       new ServiceProcessInstaller();
        ServiceInstaller serviceInstaller = new ServiceInstaller();

        public MyServiceInstaller()
        {
            //Who will run this service?
            serviceProcessInstaller.Account = ServiceAccount.LocalSystem;
            //Set login credentials if needed
            //serviceProcessInstaller.Username = ?;
            //serviceProcessInstaller.Password = ?;

            //This is the name that is displayed in the service menu
            serviceInstaller.DisplayName = AppConfig.ServiceName;
            serviceInstaller.Description = AppConfig.ServiceDescription;

            //How will the service act on startup?
            serviceInstaller.StartType = ServiceStartMode.Automatic;

            //The actual name of the service, must be the same
            //as in your ServiceBase
            serviceInstaller.ServiceName = AppConfig.ServiceName;

            this.Installers.Add(serviceProcessInstaller);
            this.Installers.Add(serviceInstaller);

            this.AfterInstall += new InstallEventHandler(ServiceInstaller_AfterInstall);
        }


        void ServiceInstaller_AfterInstall(object sender, InstallEventArgs e)
        {
            using (ServiceController sc = new ServiceController(serviceInstaller.ServiceName))
            {
                sc.Start();
            }
        }
    }
}
