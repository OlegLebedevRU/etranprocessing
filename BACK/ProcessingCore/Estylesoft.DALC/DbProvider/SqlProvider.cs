using System;
using System.Data;
using System.Data.SqlClient;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Реализация <see cref="IDbProvider"/> 
	/// для <see cref="System.Data.SqlClient"/>.
	/// </summary>
	public class SqlProvider : IDbProvider
	{
		/// <summary>
		/// Инициализирует новый экземпляр класса <see cref="SqlProvider"/>.
		/// </summary>
		/// <remarks>
		/// Параметр <see cref="ExceptionProvider"/> инициализируется 
		/// новым экземпляром класса <see cref="SqlExceptionProvider"/>.
		/// </remarks>
		public SqlProvider()
		{
			_exceptionProvider = new SqlExceptionProvider();
		}

		/// <summary>
		/// Создать объект - команду.
		/// </summary>
		/// <returns>Команда.</returns>
		public System.Data.IDbCommand CreateCommand()
		{
			return new SqlCommand();
		}

		/// <summary>
		/// Создать объект - параметр команды.
		/// </summary>
		/// <returns>Параметр команды.</returns>
		public System.Data.IDataParameter CreateParameter()
		{
			return new SqlParameter();
		}

		/// <summary>
		/// Создать объект - подключение к БД.
		/// </summary>
		/// <returns>Подключение.</returns>
		public System.Data.IDbConnection CreateConnection()
		{
			return new SqlConnection();
		}

		/// <summary>
		/// Создать объект - DataAdapter.
		/// </summary>
		/// <returns>DataAdapter.</returns>
		public System.Data.IDbDataAdapter CreateDataAdapter()
		{
			return new SqlDataAdapter();
		}
		
		/// <summary>
		/// Заполнить коллекцию параметров команды.
		/// </summary>
		/// <param name="command">Команда.</param>
		public void DeriveParameters(System.Data.IDbCommand command)
		{
			SqlCommandBuilder.DeriveParameters((SqlCommand)command);
		}

		/// <summary>
		/// Преобразовать исключение, специфичное для провайдера БД в 
		/// исключение типа DbException или производного от него.
		/// </summary>
		/// <param name="exception">Исключение, специфичное для провайдера БД.</param>
		/// <returns>Исключение типа DbException или производного от него.</returns>
		public System.Exception TransformException(System.Exception exception)
		{
			if(_exceptionProvider == null)
			{
				if(exception is SqlException)
					return new DbException(exception);
				else
					return exception;
			}
			else
                return _exceptionProvider.TransformException(exception);
		}

		private IDbExceptionProvider _exceptionProvider;
		/// <summary>
		/// Класс, который будет использоватся методом <see cref="TransformException"/>
		/// </summary>
		public IDbExceptionProvider ExceptionProvider
		{
			get { return _exceptionProvider; }
			set { _exceptionProvider = value; }
		}

		/// <summary>
		/// Имя провайдера.
		/// </summary>
		public string ProviderName
		{
			get
			{
				return "sql";
			}
		}
	}
}
