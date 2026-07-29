using System;
using System.Collections;
using System.Text;
using System.Runtime.Serialization;

namespace Estylesoft.Etran
{
	/// <summary>
	/// Запрос к платежной системе Рапида.
	/// </summary>
	[Serializable]
	public sealed class RequestMessage
	{
		private int _paymentID;
		private readonly MessageFunctions _function;
		private readonly string _paymExtId;
		private int _paymSubjTp;
        private int _PaymState;
		private long _amount;
        private long _alt_amount;
		private string _params;
        private int _serial;
		private int _totalsum;
        private string _signature;
		private string _url;
		private string _rek;
        private int _ps_id;
        private int _user_id;
        private int _kopeks;
        private int _payTypeId;




        /// <summary>
        /// Преобразует таблицу параметров в строку.
        /// </summary>
        /// <param name="Params">Таблица параметров запроса, где ключ - целое число, а значение - значение параметра в сртоковом виде.</param>
        /// <returns>Строку параметров в формате Rapida.</returns>
        static public string HashtableToParamsString(Hashtable Params)
		{
			StringBuilder paramsString = new StringBuilder();
			foreach(DictionaryEntry param in Params)
			{
				paramsString.Append(param.Key.ToString() + " " + param.Value.ToString());
				paramsString.Append(';');
			}

			if (paramsString.Length > 0)
			{
				paramsString.Remove(paramsString.Length - 1,1);
			}

			return paramsString.ToString();
		}

		/// <summary>
		/// Преобразует строку параметров в формате Rapida в таблицу параметров.
		/// </summary>
		/// <param name="Params">Строка параметров в формате Rapida.</param>
		/// <returns>Таблицу параметров запроса, где ключ - целое число, а значение - значение параметра в сртоковом виде.</returns>
        //static public Hashtable ParamsStringToHashtable(string Params)
        //{
        //    Hashtable paramsTable = new Hashtable();

        //    string[] paramEntries = Params.Split(';');

        //    if (paramEntries.Length > 1 || paramEntries[0] != string.Empty)
        //    {
        //        foreach(string param in paramEntries)
        //        {
        //            int indx = param.IndexOf(' ');
        //            string key = param.Substring(0, indx);
        //            string val = param.Substring(indx + 1, param.Length - indx - 1);
        //            paramsTable.Add(Convert.ToInt32(key), val);

        //            //string[] keyValue = param.Split(' ');

        //            //if (keyValue.Length != 2)
        //            //{
        //            //    throw new EtranException(typeof(RequestMessage).FullName, "ParamsStringToHashtable", "Не правильный формат строки дополнительных параметров", null);
        //            //}

        //            //paramsTable.Add(Convert.ToInt32(keyValue[0].Trim()), keyValue[1].Trim());
        //        }
        //    }

        //    return paramsTable;
        //}
        static public Hashtable ParamsStringToHashtable(string Params)
        {
            Hashtable paramsTable = new Hashtable();
            string[] paramEntries = Params.Split(';');
            if (paramEntries.Length > 1 || paramEntries[0] != string.Empty)
            {
                foreach (string param in paramEntries)
                {
                    int indx;
                    string key = string.Empty;
                    string val = string.Empty;

                    try
                    {
                        indx = param.IndexOf('=');
                        if (indx < 0)
                            indx = param.IndexOf(' ');

                        key = param.Substring(0, indx);
                        val = param.Substring(indx + 1, param.Length - indx - 1);
                    }
                    catch (Exception ex)
                    {
                        int h = 0;
                    }
                    paramsTable.Add(key, val);
                }
            }
            return paramsTable;
        }


		/// <summary>
		/// Конструктор.
		/// </summary>
		/// <param name="PaymentID">Идентификатор платежа в БД Etran.</param>
		/// <param name="Function">Функция сообщения (check,payment)</param>
		/// <param name="PaymentExtId">Уникальный номер платежа.</param>
		/// <param name="PaymSubjTp">Код  получателя платежа в системе Рапида.</param>
		/// <param name="Amount">Сумма платежа.</param>
		/// <param name="Params">Коды и значения параметров платежа.</param>
		public RequestMessage(int PaymentID,string Function,string PaymExtId,string PaymSubjTp,string Amount,string Params,
            int serial, int totalsum, string signature, string url, string rek, int ps_id, int user_id, int kopeks, int payTypeId)
		{
			_paymentID = PaymentID;
			_function = (MessageFunctions)Enum.Parse(typeof(MessageFunctions),Function,true);
			_paymExtId = PaymExtId;
			_paymSubjTp = Convert.ToInt32(PaymSubjTp);
			_amount = long.Parse(Amount);
			_params = Params;
			_serial = serial;
			_totalsum = totalsum;
            _signature = signature;
			_url = url;
			_rek = rek;
            _ps_id = ps_id;
            _user_id = user_id;
		    _kopeks = kopeks;
            _payTypeId = payTypeId;
        }

		/// <summary>
		/// Конструктор.
		/// </summary>
		/// <param name="PaymentID">Идентификатор платежа в БД Etran.</param>
		/// <param name="Function">Функция сообщения (check,payment)</param>
		/// <param name="PaymExtId">Уникальный номер платежа.</param>
		/// <param name="PaymSubjTp">Код  получателя платежа в системе Рапида.</param>
		/// <param name="Amount">Сумма платежа.</param>
		/// <param name="Params">Коды и значения параметров платежа в виде хеш-таблицы.</param>
		public RequestMessage(int PaymentID,MessageFunctions Function,string PaymExtId,int PaymSubjTp,long Amount,Hashtable Params,
            int serial, int totalsum, string signature, string url, string rek, int ps_id, int user_id, int kopeks, int payTypeId)
		{
			_paymentID = PaymentID;
			_function = Function;
			_paymExtId = PaymExtId;
			_paymSubjTp = PaymSubjTp;
			_amount = Amount;
			_params = HashtableToParamsString(Params);
			_serial = serial;
			_totalsum = totalsum;
            _signature = signature;
			_url = url;
			_rek = rek;
            _ps_id = ps_id;
            _user_id = user_id;
            _kopeks = kopeks;
            _payTypeId = payTypeId;
        }

        
		/// <summary>
		/// Идентификатор платежа в БД Etran.
		/// </summary>
		public int PaymentID 
		{
			get{ return _paymentID; }
			set{ _paymentID = value; }
		}


		/// <summary>
		/// Функция сообщения.
		/// </summary>
		public Estylesoft.Etran.MessageFunctions Function
		{
			get
			{
				return _function;
			}
		}

		/// <summary>
		/// Уникальный номер платежа.При запросе на платеж, этот код должен соответствовать
		/// коду в запросе на проверку. 
		/// </summary>
		public string PaymExtId
		{
            get
            {
                return _paymExtId;
            }
        }

		/// <summary>
		/// Код получателя платежа в платежной системе.
		/// </summary>
		public int PaymSubjTp
		{
			get
			{
				return _paymSubjTp;
			}
			set
			{
				_paymSubjTp = value;
			}
		}

       	/// <summary>
		/// Состояние платежа.
		/// </summary>
        public int PaymState
		{
			get
			{
                return _PaymState;
			}
			set
			{
                _PaymState = value;
			}
		}


        /// <summary>
        /// Сумма платежа.
        /// </summary>
        public long Amount
        {
            get
            {
                return _amount;
            }
            set
            {
                _amount = value;
            }
        }
        /// <summary>
        /// Альтернативная Сумма платежа.
        /// </summary>
        public long AltAmount
        {
            get
            {
                return _alt_amount;
            }
            set
            {
                _alt_amount = value;
            }
        }

		/// <summary>
		/// Дополнительные параметры платежа. Зависит от реализации.
		/// </summary>
		public string Params
		{
			get
			{
				return _params;
			}
			set
			{
				_params = value;
			}
		}

		
		public int Serial
		{
			get
			{
				return _serial;
			}
			set
			{
				_serial = value;
			}
		}

		public int TotalSum
		{
			get
			{
				return _totalsum;
			}
			set
			{
				_totalsum = value;
			}
		}

        public string Signature
        {
            get
            {
                return _signature;
            }
            set
            {
                _signature = value;
            }
        }

		public string Url
		{
			get
			{
				return _url;
			}
			set
			{
				_url = value;
			}
		}

		public string Rek
		{
			get
			{
				return _rek;
			}
			set
			{
				_rek = value;
			}
		}

		
		public int Ps_Id
		{
			get
			{
				return _ps_id;
			}
            set
            {
                _ps_id = value;
            }
		}

        public int UserId
        {
            get
            {
                return _user_id;
            }
            set
            {
                _user_id = value;
            }
        }
        public int Kopeks
        {
            get
            {
                return _kopeks;
            }
            set
            {
                _kopeks = value;
            }
        }

        public int PayTypeId
        {
            get
            {
                return _payTypeId;
            }
            set
            {
                _payTypeId = value;
            }
        }


        /// <summary>
        /// Возвращает таблицу дополнительных параметров
        /// </summary>
        /// <returns></returns>
        public Hashtable GetParams()
		{
			return ParamsStringToHashtable(_params);
		}

	}
}
