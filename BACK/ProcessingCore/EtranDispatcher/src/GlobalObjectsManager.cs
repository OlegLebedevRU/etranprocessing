using System;
using System.Web;
using System.Data;
using log4net;

namespace EtranDispatcher
{


	/// <summary>
    /// Class holding the application's global objects. Not intended
    /// to be instantiated. All members are static.
    /// </summary>
	public sealed class GlobalObjectsManager
	{
		private GlobalObjectsManager() {}

		static private ILog _logger = LogManager.GetLogger("application-log");

        static private string _paymentDbConnectionString = "";
        /// <summary>
        /// Connection string to the legacy Payments database (MSSQL),
        /// used by the simplified direct DB write path in Dispatcher.cs
        /// (see SimplifiedPutPayment), without going through the shared
        /// SOAP service MessageProcessor.asmx. Fetched dynamically via
        /// the EtranConfig service, the same way licensebilling/TechGate/
        /// GateGauge and MessageProcessor.asmx.cs itself already do, so
        /// no credentials need to be stored in web.config.
        /// </summary>
        static public string PaymentDbConnectionString
        {
            get
            {
                if (string.IsNullOrEmpty(_paymentDbConnectionString))
                {
                    _paymentDbConnectionString = GetDBConn(EtranConfigurationManager.DBConnNamePayments);
                    Logger.Info("PaymentDbConnectionString: retrieved");
                }
                return _paymentDbConnectionString;
            }
        }

        static private string GetDBConn(string name)
        {
            string ret = string.Empty;
            try
            {
                ret = (new System.Net.WebClient()).DownloadString(EtranConfigurationManager.EtranConfig + "?function=dbconn&dbname=" + name);
            }
            catch (Exception ex)
            {
                Logger.Error("GetDBConn", ex);
            }
            return ret;
        }

        /// <summary>
        /// Application startup initialization.
        /// </summary>
        static public void Init()
        {
            log4net.Config.XmlConfigurator.Configure();
        }


		/// <summary>
		/// Returns the application's logger instance.
		/// </summary>
		static public ILog Logger
		{
			get
			{
				return _logger;
			}
		}

	}
}
