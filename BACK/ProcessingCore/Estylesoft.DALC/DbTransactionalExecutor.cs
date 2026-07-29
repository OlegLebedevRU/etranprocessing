using System;
using System.Collections;
using System.Data;

using Estylesoft.DALC.DeclarativeTransaction;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Содержит набор статичных методов для выполнения IDbCommand.
	/// Аналог класса <see cref="DbExecutor"/>, за исключением того, 
	/// что команды всегда выполняются в транзакции.
	/// </summary>
	public class DbTransactionalExecutor
	{
		/// <summary>
		/// Выполняет <see cref="IDbCommand"/> и возвращает значение первого 
		/// поля первой строки результата выполнения команды.
		/// </summary>
		/// <param name="command">Команда</param>
		/// <param name="connection"></param>
		/// <param name="outParams">Hashtable, в которую будут помещены значения OUTPUT и RETURN параметров команды.</param>
		/// <returns>Значение первого поля первой строки результата выполнения команды.</returns>
		public static object ExecuteScalar(IDbCommand command, DbConnection connection, out Hashtable outParams)
		{
			using(TransactionContext tc = DbManager.BeginTransaction(true))
			{
				try
				{
					return DbExecutor.ExecuteScalar(command, connection, out outParams);
				}
				catch
				{
					tc.Rollback();
					throw;
				}
			}
		}


		/// <summary>
		/// Выполняет <see cref="IDbCommand"/> 
		/// и возвращает значение первого поля первой строки результата 
		/// выполнения команды.
		/// </summary>
		/// <param name="command">Команда</param>
		/// <param name="connection"></param>
		/// <returns>Значение первого поля первой строки результата выполнения команды.</returns>
		public static object ExecuteScalar(IDbCommand command, DbConnection connection)
		{
			using(TransactionContext tc = DbManager.BeginTransaction(true))
			{
				try
				{
					return DbExecutor.ExecuteScalar(command, connection);
				}
				catch
				{
					tc.Rollback();
					throw;
				}
			}
		}


		/// <summary>
		/// Выполняет <see cref="IDbCommand"/> используя подключение "по умолчанию"
		/// и возвращает значение первого 
		/// поля первой строки результата выполнения команды.
		/// </summary>
		/// <param name="command">Команда</param>
		/// <returns>Значение первого поля первой строки результата выполнения команды.</returns>
		public static object ExecuteScalar(IDbCommand command)
		{
			return ExecuteScalar(command, DbConnectionsManager.Default);
		}
		
		
		/// <summary>
		/// Выполняет <see cref="IDbCommand"/> и возвращает количество 
		/// обработанных записей.
		/// </summary>
		/// <param name="command">Команда</param>
		/// <param name="connection"></param>
		/// <param name="outParams">Hashtable, в которую будут помещены значения OUTPUT и RETURN параметров команды.</param>
		/// <returns>Количество обработанных записей.</returns>
		public static int ExecuteNonQuery(IDbCommand command, DbConnection connection, out Hashtable outParams)
		{
			using(TransactionContext tc = DbManager.BeginTransaction(true))
			{
				try
				{
					return DbExecutor.ExecuteNonQuery(command, connection, out outParams);
				}
				catch
				{
					tc.Rollback();
					throw;
				}
			}
		}


		/// <summary>
		/// Выполняет <see cref="IDbCommand"/> и возвращает количество 
		/// обработанных записей.
		/// </summary>
		/// <param name="command">Команда</param>
		/// <param name="connection"></param>
		/// <returns>Количество обработанных записей.</returns>
		public static int ExecuteNonQuery(IDbCommand command, DbConnection connection)
		{
			using(TransactionContext tc = DbManager.BeginTransaction(true))
			{
				try
				{
					return DbExecutor.ExecuteNonQuery(command, connection);
				}
				catch
				{
					tc.Rollback();
					throw;
				}
			}
		}
		

		/// <summary>
		/// Выполняет <see cref="IDbCommand"/> используя подключение "по умолчанию",
		/// и возвращает количество обработанных записей.
		/// </summary>
		/// <param name="command">Команда</param>
		/// <returns>Количество обработанных записей.</returns>
		public static int ExecuteNonQuery(IDbCommand command)
		{
			return ExecuteNonQuery(command, DbConnectionsManager.Default);
		}

		
		/// <summary>
		/// Возвращает <see cref="DataSet"/>, содержащий результат
		/// выполнения <see cref="IDbCommand"/>.
		/// </summary>
		/// <param name="command">Команда</param>
		/// <param name="connection"></param>
		/// <param name="outParams">Hashtable, в которую будут помещены 
		/// значения OUTPUT и RETURN параметров команды.</param>
		/// <returns><see cref="DataSet"/>, содержащий результат выполнения команды.</returns>
		public static DataSet ExecuteDataSet(IDbCommand command, DbConnection connection, out Hashtable outParams)
		{
			using(TransactionContext tc = DbManager.BeginTransaction(true))
			{
				try
				{
					return DbExecutor.ExecuteDataSet(command, connection, out outParams);
				}
				catch
				{
					tc.Rollback();
					throw;
				}
			}
		}


		/// <summary>
		/// Возвращает <see cref="DataSet"/>, содержащий результат
		/// выполнения <see cref="IDbCommand"/>.
		/// </summary>
		/// <param name="command">Команда</param>
		/// <param name="connection"></param>
		/// <returns><see cref="DataSet"/>, содержащий результат выполнения команды.</returns>
		public static DataSet ExecuteDataSet(IDbCommand command, DbConnection connection)
		{
			using(TransactionContext tc = DbManager.BeginTransaction(true))
			{
				try
				{
					return DbExecutor.ExecuteDataSet(command, connection);
				}
				catch
				{
					tc.Rollback();
					throw;
				}
			}
		}


		/// <summary>
		/// Возвращает <see cref="DataSet"/>, содержащий результат
		/// выполнения <see cref="IDbCommand"/>. 
		/// Для выполнения команды используется подключение "по умолчанию".
		/// </summary>
		/// <param name="command">Команда</param>
		/// <returns><see cref="DataSet"/>, содержащий результат выполнения команды.</returns>
		public static DataSet ExecuteDataSet(IDbCommand command)
		{
			return ExecuteDataSet(command, DbConnectionsManager.Default);
		}


		/// <summary>
		/// Выполняет <see cref="IDbCommand"/> и возвращает 
		/// значения output параметров.
		/// </summary>
		/// <param name="command">Команда</param>
		/// <param name="connection"></param>
		/// <returns>HashTable, содержащая значения output параметров.</returns>
		public static Hashtable ExecuteHashTable(IDbCommand command, DbConnection connection)
		{
			using(TransactionContext tc = DbManager.BeginTransaction(true))
			{
				try
				{
					return DbExecutor.ExecuteHashTable(command, connection);
				}
				catch(System.Exception ex)
				{
					tc.Rollback();
					throw ex;
				}
			}
		}


		/// <summary>
		/// Выполняет <see cref="IDbCommand"/> и возвращает 
		/// значения output параметров.
		/// Для выполнения команды используется подключение "по умолчанию".
		/// </summary>
		/// <param name="command">Команда</param>
		/// <returns>HashTable, содержащая значения output параметров.</returns>
		public static Hashtable ExecuteHashTable(IDbCommand command)
		{
			return ExecuteHashTable(command, DbConnectionsManager.Default);
		}
	}
}
