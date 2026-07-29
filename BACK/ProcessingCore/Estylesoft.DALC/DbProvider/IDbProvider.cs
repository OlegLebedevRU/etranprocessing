using System;
using System.Data;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Интерфейс, который должен реализовывать класс, 
	/// предоставляющий методы для создания команд, параметров команд, 
	/// подключений и т.п. объектов, специфичных для конкретного 
	/// провайдера БД. 
	/// </summary>
	public interface IDbProvider
	{
		/// <summary>
		/// Имя провайдера.
		/// </summary>
		string ProviderName
		{
			get;
		}

		/// <summary>
		/// Создать объект - подключение к БД.
		/// </summary>
		IDbConnection CreateConnection();

		/// <summary>
		/// Создать объект - команду.
		/// </summary>
		IDbCommand CreateCommand();

		/// <summary>
		/// Создать объект - параметр команды.
		/// </summary>
		IDataParameter CreateParameter();

		/// <summary>
		/// Создать объект - DataAdapter.
		/// </summary>
		IDbDataAdapter CreateDataAdapter();

		/// <summary>
		/// Преобразовать исключение, специфичное для провайдера БД в 
		/// исключение типа DbException или производного от него.
		/// </summary>
		/// <param name="exception">Исключение, специфичное для провайдера БД.</param>
		/// <returns>Исключение типа DbException или производного от него.</returns>
		System.Exception TransformException(System.Exception exception);

		/// <summary>
		/// Класс, который будет использоватся методом <see cref="TransformException"/>
		/// </summary>
		IDbExceptionProvider ExceptionProvider
		{
			get;
			set;
		}

		/// <summary>
		/// Заполнить коллекцию параметров команды.
		/// </summary>
		/// <param name="command">Команда.</param>
		void DeriveParameters(IDbCommand command);
	}
}
