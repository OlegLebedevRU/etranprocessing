using System;
using System.Web;
using System.Data;
using log4net;

namespace EtranDispatcher
{


	/// <summary>
    /// �����, ��������� � ����������� ����� ����������� ��������� ������. �� ����� ����������
    /// ����� �� ��������. ��� ������ ����������������.
    /// </summary>
	public sealed class GlobalObjectsManager
	{
		private GlobalObjectsManager() {}

		static private ILog _logger = LogManager.GetLogger("application-log");

        static private string _paymentDbConnectionString = "";
        /// <summary>
        /// Строка подключения к легаси-БД Payments (MSSQL), используется
        /// упрощённым путём фиксации платежа в Dispatcher.cs (см.
        /// SimplifiedPutPayment) напрямую, без общего SOAP-сервиса
        /// MessageProcessor.asmx. Получается динамически через сервис
        /// EtranConfig — так же, как это уже делают licensebilling/
        /// TechGate/GateGauge и сам MessageProcessor.asmx.cs, чтобы не
        /// хранить строку подключения с учётными данными в web.config.
        /// </summary>
        static public string PaymentDbConnectionString
        {
            get
            {
                if (string.IsNullOrEmpty(_paymentDbConnectionString))
                {
                    _paymentDbConnectionString = GetDBConn(EtranConfigurationManager.DBConnNamePayments);
                    Logger.Info("PaymentDbConnectionString: получена");
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
        /// �������������� �������� ���������� ��������.
        /// </summary>
        static public void Init()
        {
            log4net.Config.XmlConfigurator.Configure();
        }


		/// <summary>
		/// ���������� ������ ������� �������.
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
