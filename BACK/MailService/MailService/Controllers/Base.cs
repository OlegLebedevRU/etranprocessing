using System;
using System.Net;
using System.Threading;
using log4net;

namespace MailService.Controllers
{
    public abstract class Base
    {
        private readonly ILog _logger;

        protected ILog Logger
        {
            get { return _logger; }
        }

        public static ManualResetEvent StopActivity { get; set; }
        public abstract void SimpleAction();

        public abstract int DelayStart { get; }

        public abstract int SleepInterval { get; }
        private readonly Thread _threadController;


        protected Base()
        {
            _logger = LogManager.GetLogger(GetType().Name);
            _threadController = new Thread(Controller) { IsBackground = true };
            _threadController.Start();
        }


        private void Controller()
        {
            StopActivity.WaitOne(DelayStart);

            Logger.Info("Controller" + GetType().FullName + " START");
            ServicePointManager.SecurityProtocol = SecurityProtocolType.Ssl3 | SecurityProtocolType.Tls | SecurityProtocolType.Tls11 | SecurityProtocolType.Tls12;
            do
            {
                if (StopActivity.WaitOne(1000))
                    break;

                try
                {
                    Logger.Info("Controller" + GetType().FullName + " TRY...");
                    SimpleAction();
                }
                catch (Exception ex)
                {
                    Logger.Error("Controller"+ GetType().FullName, ex);
                }

                Logger.Info("Controller " + GetType().FullName + " WAIT FOR TIME " + SleepInterval);

            } while (!StopActivity.WaitOne(SleepInterval));

            Logger.Info("Controller" + GetType().FullName + " DONE");
        }
    }
}
