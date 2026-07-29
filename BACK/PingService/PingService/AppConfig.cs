
using log4net;
using System.Configuration;

namespace PingService
{
    public class AppConfig
    {
        static public ILog Logger = LogManager.GetLogger(typeof(AppConfig));


        public static string ServiceName { get { return ConfigurationManager.AppSettings["ServiceName"]; } }

        public static string ServiceDescription { get { return ConfigurationManager.AppSettings["ServiceDescription"]; } }


        static public string PingResource
        {
            get
            {
                return ConfigurationManager.AppSettings["PingResource"];
            }
        }

        static public string Run
        {
            get
            {
                return ConfigurationManager.AppSettings["Run"];
            }
        }
        static public string Arguments
        {
            get
            {
                return ConfigurationManager.AppSettings["Arguments"];
            }
        }

        public static int StartAfter { get { return int.Parse(ConfigurationManager.AppSettings["StartAfter"]); } }

        public static int SleepInterval { get { return int.Parse(ConfigurationManager.AppSettings["SleepInterval"]); } }

        public static int MaxAttemptFault { get { return int.Parse(ConfigurationManager.AppSettings["MaxAttemptFault"]); } }
    }
}
