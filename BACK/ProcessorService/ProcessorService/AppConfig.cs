
using log4net;
using System.Configuration;

namespace ProcessorService
{
    public class AppConfig
    {
        static public ILog Logger = LogManager.GetLogger(typeof(AppConfig));


        public static string ServiceName { get { return ConfigurationManager.AppSettings["ServiceName"]; } }

        public static string ServiceDescription { get { return ConfigurationManager.AppSettings["ServiceDescription"]; } }

        public static string PaySystem { get { return ConfigurationManager.AppSettings["PaySystem"]; } }
        public static string SmsService { get { return ConfigurationManager.AppSettings["SmsService"]; } }

        public static string Rek { get { return ConfigurationManager.AppSettings["Rek"]; } }

        public static string Function { get { return ConfigurationManager.AppSettings["Function"]; } }

        public static int StartAfter { get { return int.Parse(ConfigurationManager.AppSettings["StartAfter"]); } }

        public static int SleepInterval { get { return int.Parse(ConfigurationManager.AppSettings["SleepInterval"]); } }

    }
}
