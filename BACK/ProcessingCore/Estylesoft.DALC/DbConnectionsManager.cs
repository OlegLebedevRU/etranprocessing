using System;
using System.Collections;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Класс, хранящий коллекцию подключений к БД, используемых в приложении.
	/// </summary>
	/// <remarks>
	/// Подключения регистрируются с помощью метода <see cref="RegisterConnection"/>.
	/// При регистрации подключения указывается провайдер (<see cref="IDbProvider"/>), 
	/// который будет использоваться при работе с этим подключением. 
	/// Перед использованием провайдера, его нужно зарегистрировать - 
	/// для этого используется метод <see cref="RegisterProvider"/>.
	/// <para>
	/// Одно из подключений может быть объявлено как подключение "по умолчанию". 
	/// Для доступа к нему используется свойство Default или метод <see cref="Get"/> без параметра. 
	/// Подключение, которое первым было побавлено в коллекцию. автоматически 
	/// становится подключением "по умолчанию" до тех пор, пока не будет вызван 
	/// метод <see cref="SetDefaultConnection"/>.
	/// </para>
	/// <para>
	/// Класс не является подключением к БД в прямом смысле - он только хранит 
	/// информацию, необходимую для создания реального подключения (SqlConnection, 
	/// OleDbConnection) и работы с нам.
	/// </para>
	/// </remarks>
	public class DbConnectionsManager
	{
		private DbConnectionsManager()
		{
		}

		private static Hashtable _connections = new Hashtable();

		private static Hashtable _providers = new Hashtable();

		private static string _defaultConnectionKey = null;

		/// <summary>
		/// Установить подключение с указанным Key, 
		/// как подключение "по умолчанию".
		/// </summary>
		/// <param name="connectionKey">Ключ подключения.</param>
		public static void SetDefaultConnection(string connectionKey)
		{
			_defaultConnectionKey = connectionKey;
		}

		/// <summary>
		/// Получить информацию о подключении по его Key.
		/// </summary>
		/// <param name="connectionKey"></param>
		/// <returns>Информация о подключении.</returns>
		public static DbConnection Get(string connectionKey)
		{
			return (DbConnection)_connections[connectionKey];
		}

		/// <summary>
		/// Получить информацию о подключении "по умолчанию".
		/// </summary>
		/// <returns>Информация о подключении.</returns>
		public static DbConnection Get()
		{
			return Get(_defaultConnectionKey);
		}
	
		/// <summary>
		/// Получить информацию о подключении "по умолчанию".
		/// </summary>
		public static DbConnection Default
		{
			get { return Get(); }
		}

		/// <summary>
		/// Зарегистрировать провайдер.
		/// </summary>
		/// <param name="provider">Провайдер.</param>
		public static void RegisterProvider(IDbProvider provider)
		{
			_providers.Add(provider.ProviderName, provider);
		}
		
		/// <summary>
		/// Зарегистрировать информацию о подключении.
		/// </summary>
		/// <param name="key">Ключ подключения.</param>
		/// <param name="connectionString">Строка подключения.</param>
		/// <param name="provider">Наименование провайдера.</param>
		public static void RegisterConnection(string key, string connectionString, string provider)
		{
			DbConnection connection = new DbConnection();

			connection.ConnectionString = connectionString;
			connection.DbProvider = (IDbProvider)_providers[provider];
			
			RegisterConnection(key, connection);
		}

		/// <summary>
		/// Зарегистрировать информацию о подключении.
		/// </summary>
		/// <param name="key">Ключ подключения.</param>
		/// <param name="connection">Подключение.</param>
		public static void RegisterConnection(string key, DbConnection connection)
		{
			_connections.Add(key, connection);

			if(_connections.Count == 1)
				SetDefaultConnection(key);
		}
	}
}
