using System;
using System.Web;
using System.Data;
using log4net;

namespace EtranDispatcher
{


	/// <summary>
    /// Класс, создающий и управляющий всеми глобальными объектами службы. Он также поределяет
    /// место их хранения. Все методы потокобезопасные.
    /// </summary>
	public sealed class GlobalObjectsManager
	{
		private GlobalObjectsManager() {}

		static private ILog _logger = LogManager.GetLogger("application-log");

        /// <summary>
        /// Инициализирует менеджер глобальных объектов.
        /// </summary>
        static public void Init()
        {
            log4net.Config.XmlConfigurator.Configure();
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
