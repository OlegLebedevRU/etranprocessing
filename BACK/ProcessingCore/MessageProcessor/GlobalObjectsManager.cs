using System;
using System.Web;
using System.Collections;
using System.Configuration;
using log4net;
using Estylesoft.Etran;
using System.Threading;
using EtranLib.Net;


namespace MessageProcessor
{

    public class Logger
    {
        static private ILog _logger_exc = LogManager.GetLogger("LoggerExc");
        static private ILog _logger_info = LogManager.GetLogger("LoggerInfo");

        public Logger()
        {
        }

        public void Init()
        {
            log4net.Config.XmlConfigurator.Configure();
        }

        public void Info(object msg)
        {
            _logger_info.Info(msg);
        }

        public void Error(object msg)
        {
            _logger_exc.Error(msg);
        }

        public void Error(object msg, Exception e)
        {
            _logger_exc.Error(msg,e);
        }

    }

	/// <summary>
	/// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
	/// место их хранения. Все методы потокобезопасные.
	/// </summary>
	public sealed class GlobalObjectsManager
	{
		private GlobalObjectsManager() {}

		//static private ILog _logger = LogManager.GetLogger("application-log");
        static public Logger Logger = new Logger();
        static public long JobsCounter = 0;
        private const string DB_INTERFACE_KEY = "DbInterface";
        private static HttpApplicationState _application = null;
        static public Hashtable PaymentList = new Hashtable();
        static public bool IsRunApp = false;
        static public string PaymentDbConnectionString;
        static public string ServiceDbConnectionString;

        static public string GetDBConn(string name)
        {
            string ret = string.Empty;
            try
            {
                ret = (new System.Net.WebClient()).DownloadString(EtranConfigurationManager.EtranConfig+"?function=dbconn&dbname=" + name);
            }
            catch (Exception ex)
            {
                Logger.Error("GetDBConn", ex);
            }
            return ret;
        }

        /// <summary>
        /// Инициализирует менеджер глобальных объектов.
        /// </summary>
        static public void Init()
        {
            //string f = Environment.GetEnvironmentVariable("ProCpec");
            //if (f != "2866ddb1179ca3e470cb964bbdd388f4")
            //    return;

            Logger.Init();
            PaymentDbConnectionString = GetDBConn(EtranConfigurationManager.DBConnNamePayments);
            ServiceDbConnectionString = GetDBConn(EtranConfigurationManager.DBConnNameService);

            Net.Init();
            TimeZone.Init();
            IsRunApp = true;
            HttpContext ctx = HttpContext.Current;

            if (ctx != null)
            {
                _application = ctx.Application;
            }
            else
            {
                throw new EtranException(typeof(GlobalObjectsManager).FullName, "Init", "Невозможно получить HTTP контекст.", null);
            }

        }

        static public void UnInit()
        {
            IsRunApp = false;
            TimeZone.UnInit();
        }


        /// <summary>
        /// Экземпляр объекта Application текущего контекста.
        /// </summary>
        static public HttpApplicationState Application
        {
            get
            {
                return _application;
            }
            set
            {
                _application = value;
            }
        }

        /// <summary>
        /// Singleton интерфейса БД.
        /// </summary>
        static private DbInterface DbInterface
        {
            get
            {
                return Application[DB_INTERFACE_KEY] as DbInterface;
            }
            set
            {
                Application[DB_INTERFACE_KEY] = value;
            }
        }

        /// <summary>
        /// Создает singleton интерфейса БД.
        /// </summary>
        static public void CreateDbInterface()
        {
            lock (Application)
            {
                DbInterface dbInterface = DbInterface;

                if (dbInterface != null)
                {
                    return;
                }

                dbInterface = new DbInterface();

                DbInterface = dbInterface;
            }
        }

        /// <summary>
        /// Удаляет singleton интерфейса БД.
        /// </summary>
        static public void DeleteDbInterface()
        {
            lock (Application)
            {
                DbInterface = null;
            }
        }

        /// <summary>
        /// Возвращает глобальный экземпляр интерфейса БД.
        /// </summary>
        /// <returns></returns>
        static public DbInterface GetDbInterface()
        {
            DbInterface dbInterface = DbInterface;

            if (dbInterface == null)
            {
                CreateDbInterface();

                dbInterface = DbInterface;

                if (dbInterface == null)
                {
                    throw new EtranException(typeof(GlobalObjectsManager).FullName, "GetDbInterface", "Ошибка получения интерфейса базы данных.", null);
                }
            }

            return dbInterface;
        }


	}
}
