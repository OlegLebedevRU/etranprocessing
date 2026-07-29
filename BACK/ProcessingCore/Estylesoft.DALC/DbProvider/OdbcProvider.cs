using System;
using System.Data.Odbc;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Реализация <see cref="IDbProvider"/> 
	/// для <see cref="System.Data.Odbc"/>.
	/// </summary>
	public class OdbcProvider : IDbProvider
	{
		/// <summary>
		/// Инициализирует новый экземпляр класса <see cref="OdbcProvider"/> 
		/// </summary>
		public OdbcProvider()
		{
		}

		/// <summary>
		/// Создать объект - команду.
		/// </summary>
		/// <returns>Команда.</returns>
		public System.Data.IDbCommand CreateCommand()
		{
			return new OdbcCommand();
		}

		/// <summary>
		/// Создать объект - параметр команды.
		/// </summary>
		/// <returns>Параметр команды.</returns>
		public System.Data.IDataParameter CreateParameter()
		{
			return new OdbcParameter();
		}

		/// <summary>
		/// Создать объект - подключение к БД.
		/// </summary>
		/// <returns>Подключение.</returns>
		public System.Data.IDbConnection CreateConnection()
		{
			return new OdbcConnection();
		}

		/// <summary>
		/// Создать объект - DataAdapter.
		/// </summary>
		/// <returns>DataAdapter.</returns>
		public System.Data.IDbDataAdapter CreateDataAdapter()
		{
			return new OdbcDataAdapter();
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
				if(exception is OdbcException)
					return new DbException(exception);
				else
					return exception;
			}
			else
				return _exceptionProvider.TransformException(exception);
		}

		/// <summary>
		/// Заполнить коллекцию параметров команды.
		/// </summary>
		/// <param name="command">Команда.</param>
		public void DeriveParameters(System.Data.IDbCommand command)
		{
			OdbcCommandBuilder.DeriveParameters((OdbcCommand)command);
		}

		/// <summary>
		/// Имя провайдера.
		/// </summary>
		public string ProviderName
		{
			get
			{
				return "odbc";
			}
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
	}
}