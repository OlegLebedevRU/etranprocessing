using System;
using System.Configuration.Install;
using System.Reflection;
using System.ServiceProcess;
using System.Threading;
using log4net;
using MailService.Controllers;
using MailService.Entity;

namespace MailService
{
    class Program
    {
        static public ILog Logger = LogManager.GetLogger(typeof(Program));
        private static void CurrentDomainUnhandledException(object sender, UnhandledExceptionEventArgs e)
        {
            Logger.Error(((Exception)e.ExceptionObject).Message + ((Exception)e.ExceptionObject).InnerException.Message);
        }

        private static readonly ManualResetEvent StopActivity = new ManualResetEvent(false);
        public static Mailer Mailer;


        #region Nested classes to support running as service

        public class Service : ServiceBase
        {
            public Service()
            {
                ServiceName = AppConfig.ServiceName;
            }

            protected override void OnStart(string[] args)
            {
                Program.Start(args);
            }

            protected override void OnStop()
            {
                Program.Stop();
            }
        }
        #endregion

        private static void Start(string[] args)
        {
            Logger.Info("Service Start...");
            StopActivity.Reset();
            Mailer = new Mailer(Message.GetMessages);
        }

        private static void Stop()
        {
            Logger.Info("Service Stop...");
            StopActivity.Set();
            Thread.Sleep(100);
        }

        static void Main(string[] args)
        {
            AppDomain.CurrentDomain.UnhandledException += CurrentDomainUnhandledException;
            Base.StopActivity = StopActivity;
            //string ss = "dvkosov@gmail.com,notify.dvkosov@gmail.com";
            //var rr = Message.GetMails(ss);
            //return;


            if (!Environment.UserInteractive)
                // running as service
                using (var service = new Service())
                    ServiceBase.Run(service);
            else
            {
                string parameter = string.Concat(args);

                if (parameter.IndexOf("install", StringComparison.Ordinal) == -1)
                {
                    // running as console app
                    Start(args);
                    Console.WriteLine("Press any key to stop...");
                    Console.ReadKey(true);
                    Stop();
                }
                else
                {
                    switch (parameter)
                    {
                        case "--install":
                            ManagedInstallerClass.InstallHelper(new string[] {Assembly.GetExecutingAssembly().Location});
                            break;
                        case "--uninstall":
                            ManagedInstallerClass.InstallHelper(new string[]
                                {"/u", Assembly.GetExecutingAssembly().Location});
                            break;
                    }
                }
            }
        }
    }
}
