using System;
using log4net;

namespace PostProcessor
{
	/// <summary>
	/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
	/// место их хранения. Все методы потокобезопасные.
	/// </summary>
	public sealed class GlobalObjectsManager
	{
        static GlobalObjectsManager()
        {
            log4net.Config.XmlConfigurator.Configure();

        }
        static public readonly string ServiceName = "Platerra.PostProcessor";
        static private ILog _logger = LogManager.GetLogger("Monitor");
        static public string DbConnectionString = null;


        static public string GetDBConn(string name)
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


        static public void Init()
        {
            DbConnectionString = GetDBConn(EtranConfigurationManager.DBConnName);
            Logger.Info("DbConnectionString: " + DbConnectionString);
        }


		/// <summary>
		/// Возвращает объект журнала событий.
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
