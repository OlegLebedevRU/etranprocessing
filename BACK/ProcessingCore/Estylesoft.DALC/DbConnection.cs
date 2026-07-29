using System;
using System.Collections;
using System.Reflection;
using System.Configuration;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Класс, предоставляющий онформацию о конкретном подключении к базе данных.
	/// </summary>
	/// <remarks>
	/// Информация о подключении включает строку подключения и 
	/// объект <see cref="IDbProvider"/>, предоставляющий методы для 
	/// создания команд, параметров команд, подключений и т.д. специфичных 
	/// для конкретного провайдера.
	/// </remarks>
	public class DbConnection
	{
		/// <summary>
		/// Создает экземпляр <see cref="DbConnection"/>.
		/// </summary>
		public DbConnection()
		{}

		private string _connectionString;
		/// <summary>
		/// Строка подключения к БД.
		/// </summary>
		public string ConnectionString
		{
			get { return _connectionString; }
			set { _connectionString= value; }
		}

	
		private IDbProvider _dbProvider;
		/// <summary>
		/// объект <see cref="IDbProvider"/>, предоставляющий методы для 
		/// создания команд, параметров команд, подключений и т.д. специфичных 
		/// для конкретного провайдера.
		/// </summary>
		public IDbProvider DbProvider
		{
			get { return _dbProvider; }
			set { _dbProvider = value; }
		}
	}
}
