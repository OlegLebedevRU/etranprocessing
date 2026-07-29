
using System;
using System.Configuration;
using log4net;

namespace MailService
{
    public class AppConfig
    {
        static public ILog Logger = LogManager.GetLogger(typeof(AppConfig));


        public static string ServiceName { get { return ConfigurationManager.AppSettings["ServiceName"]; } }

        public static string ServiceDescription { get { return ConfigurationManager.AppSettings["ServiceDescription"]; } }


        public static string DbGetMessages { get { return ConfigurationManager.AppSettings["DbGetMessages"]; } }

        public static string DbPostResult { get { return ConfigurationManager.AppSettings["DbPostResult"]; } }


        static public string GetDbConn(string name)
        {
            string ret = string.Empty;
            try
            {
                Logger.Info("GetDbConn name " + name);
                ret = (new System.Net.WebClient()).DownloadString(AppConfig.EtranConfig + "?function=dbconn&dbname=" + name);
                Logger.Info("GetDbConn ret " + ret);
            }
            catch (Exception ex)
            {
                Logger.Error("GetDbConn", ex);
            }
            return ret;
        }

        private static string _dbConnectionString = string.Empty;

        static public string DbConnectionString {
            get
            {
                if (string.IsNullOrEmpty(_dbConnectionString))
                    _dbConnectionString = GetDbConn(DbConnName);
                
                return _dbConnectionString;
            }
        }


        static public string DbConnName
        {
            get
            {
                return ConfigurationManager.AppSettings["DBConnName"];
            }
        }

        static public string EtranConfig
        {
            get
            {
                return ConfigurationManager.AppSettings["EtranConfig"];
            }
        }


        public static int MailerCheckListInterval { get { return int.Parse(ConfigurationManager.AppSettings["MailerCheckListInterval"]) * 60 * 1000; } }

        public static string SmtpAddress { get { return ConfigurationManager.AppSettings["SmtpAddress"]; } }
        public static string MailFrom { get { return ConfigurationManager.AppSettings["MailFrom"]; } }
        public static string Password { get { return ConfigurationManager.AppSettings["Password"]; } }
        public static int Port { get { return int.Parse(ConfigurationManager.AppSettings["Port"]); } }
        public static bool Ssl { get { return bool.Parse(ConfigurationManager.AppSettings["Ssl"]); } }

    }
}
