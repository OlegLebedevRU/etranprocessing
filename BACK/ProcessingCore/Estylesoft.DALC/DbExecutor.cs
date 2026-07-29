using System;
using System.Data;
using System.Collections;
using System.Xml;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Содержит набор статичных методов для выполнения IDbCommand.
	/// Команды выполняются в контексте текущей транзакции, 
	/// если таковая присутствует.
	/// </summary>
	/// <remarks>
	/// В качестве параметра command можно передавать как экземпляры 
	/// уже сформированных и готовых к выполнению команд (SqlCommand, 
	/// OleDbCommand) и т.п., либо экземпляры <see cref="DbCommand"/>, 
	/// сформированные с помощью статичных методов класса 
	/// <see cref="DbCommand"/>. В последнем случае реальная команда 
	/// будет сформирована автоматически - с помощью провайдера, 
	/// соответствующего переданному экземпляру <see cref="DbConnection"/>.
	/// </remarks>
	/// <seealso cref="TransactionManager"/>
	public class DbExecutor
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
			if(!DbManager.IsTransactionOk)
				DbTransactionManagerException.ThrowException(DbTransactionManagerException.DbTransactionManagerExceptionCode.TransactionAborted);

			if(command is DbCommand)
				command = ((DbCommand)command).CreateRealCommand(connection);

			DbManager.InitCommand(command, connection);

			ConnectionState prevConnectionState = command.Connection.State;
			
			try
			{
				if(prevConnectionState == ConnectionState.Closed)
					command.Connection.Open();

				object returnValue = command.ExecuteScalar();
				outParams = GetOutputParams(command);
				return returnValue;
			}
			catch(Exception e)
			{
				throw connection.DbProvider.TransformException(e);
			}
			finally
			{
				if(prevConnectionState == ConnectionState.Closed)
					command.Connection.Close();
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
			Hashtable outParams;
			return ExecuteScalar(command, connection, out outParams);
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
			if(!DbManager.IsTransactionOk)
				DbTransactionManagerException.ThrowException(DbTransactionManagerException.DbTransactionManagerExceptionCode.TransactionAborted);

			if(command is DbCommand)
				command = ((DbCommand)command).CreateRealCommand(connection);

			DbManager.InitCommand(command, connection);

			ConnectionState prevConnectionState = command.Connection.State;
			
			try
			{
				if(prevConnectionState == ConnectionState.Closed)
					command.Connection.Open();

				int returnValue = command.ExecuteNonQuery();
				outParams = GetOutputParams(command);
				return returnValue;
			}
			catch(System.Exception e)
			{
				throw connection.DbProvider.TransformException(e);
			}
			finally
			{
				if(prevConnectionState == ConnectionState.Closed)
					command.Connection.Close();
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
			Hashtable outParams;
			return ExecuteNonQuery(command, connection, out outParams);
		}
		
		
		/// <summary>
		/// Выполняет <see cref="DbCommand"/> используя подключение "по умолчанию",
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
			if(!DbManager.IsTransactionOk)
				DbTransactionManagerException.ThrowException(DbTransactionManagerException.DbTransactionManagerExceptionCode.TransactionAborted);

			if(command is DbCommand)
				command = ((DbCommand)command).CreateRealCommand(connection);

			DbManager.InitCommand(command, connection);
			
			IDbDataAdapter dataAdapter = connection.DbProvider.CreateDataAdapter();

			dataAdapter.SelectCommand = command;
			
			DataSet dataSet = new DataSet();

			try
			{
				dataAdapter.Fill(dataSet);
				outParams = GetOutputParams(command);
				return dataSet;
			}
			catch(Exception e)
			{
				throw connection.DbProvider.TransformException(e);
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
			Hashtable outParams;
			return ExecuteDataSet(command, connection, out outParams);
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
		/// Создает DbDataReader для полученой команды.
		/// </summary>
		/// <remarks>
		/// Для метода ExecuteReader не реализован вариант с получением 
		/// output параметров, т.к. output параметры при использовании
		/// DataReader-а доступны только после закрытия reader-а.
		/// </remarks>
		/// <param name="command">Команда</param>
		/// <param name="connection"></param>
		/// <returns><see cref="DbDataReader"/>.</returns>
		public static DbDataReader ExecuteReader(IDbCommand command, DbConnection connection)
		{
			if(!DbManager.IsTransactionOk)
				DbTransactionManagerException.ThrowException(DbTransactionManagerException.DbTransactionManagerExceptionCode.TransactionAborted);

			if(command is DbCommand)
				command = ((DbCommand)command).CreateRealCommand(connection);

			DbManager.InitCommand(command, connection);

			try
			{
				if(command.Connection.State == ConnectionState.Closed)
				{
					command.Connection.Open();
					return new DbDataReader(command.ExecuteReader(CommandBehavior.CloseConnection));
				}
				else
					return new DbDataReader(command.ExecuteReader());
			}
			catch(Exception e)
			{
				throw connection.DbProvider.TransformException(e);
			}
		}

		
		/// <summary>
		/// Создает DbDataReader для полученой команды.
		/// Для выполнения команды используется подключение "по умолчанию".
		/// </summary>
		/// <remarks>
		/// Для метода ExecuteReader не реализован вариант с получением 
		/// output параметров, т.к. output параметры при использовании
		/// DataReader-а доступны только после закрытия reader-а.
		/// </remarks>
		/// <param name="command">Команда</param>
		/// <returns><see cref="DbDataReader"/>.</returns>		
		public static DbDataReader ExecuteReader(IDbCommand command)
		{
			return ExecuteReader(command, DbConnectionsManager.Default);
		}


		/// <summary>
		/// Создает XmlReader для полученой команды.
		/// </summary>
		/// <param name="command">Команда</param>
		/// <param name="connection"></param>
		/// <returns><see cref="XmlReader"/>.</returns>
		public static XmlReader ExecuteXmlReader(IDbCommand command, DbConnection connection)
		{
			if(!DbManager.IsTransactionOk)
				DbTransactionManagerException.ThrowException(DbTransactionManagerException.DbTransactionManagerExceptionCode.TransactionAborted);

			if(command is DbCommand)
				command = ((DbCommand)command).CreateRealCommand(connection);

			if(!(command is System.Data.SqlClient.SqlCommand))
				throw new NotImplementedException();

			DbManager.InitCommand(command, connection);

			try
			{
				if(command.Connection.State == ConnectionState.Closed)
				{
					command.Connection.Open();
					return ((System.Data.SqlClient.SqlCommand)command).ExecuteXmlReader();
				}
				else
					return ((System.Data.SqlClient.SqlCommand)command).ExecuteXmlReader();
			}
			catch(Exception e)
			{
				throw connection.DbProvider.TransformException(e);
			}
		}

		
		/// <summary>
		/// Создает XmlReader для полученой команды.
		/// Для выполнения команды используется подключение "по умолчанию".
		/// </summary>
		/// <returns><see cref="XmlReader"/>.</returns>
		public static XmlReader ExecuteXmlReader(IDbCommand command)
		{
			return ExecuteXmlReader(command, DbConnectionsManager.Default);
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
			if(!DbManager.IsTransactionOk)
				DbTransactionManagerException.ThrowException(DbTransactionManagerException.DbTransactionManagerExceptionCode.TransactionAborted);

			if(command is DbCommand)
				command = ((DbCommand)command).CreateRealCommand(connection);

			DbManager.InitCommand(command, connection);

			ConnectionState prevConnectionState = command.Connection.State;

			try
			{
				if(prevConnectionState == ConnectionState.Closed)
					command.Connection.Open();

				command.ExecuteNonQuery();
				return GetOutputParams(command);
			}
			catch(System.Exception e)
			{
				throw connection.DbProvider.TransformException(e);
			}
			finally
			{
				if(prevConnectionState == ConnectionState.Closed)
					command.Connection.Close();
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


		/// <summary>
		/// Возвращает хэш таблицу, заполненную значениями output 
		/// параметров команды.
		/// </summary>
		/// <param name="command">Команда</param>
		/// <returns>HashTable, содержащая значения output параметров.</returns>
		private static Hashtable GetOutputParams(IDbCommand command)
		{
			Hashtable outputParams = new Hashtable();
						
			foreach(IDataParameter parameter in command.Parameters)
			{
				if(
					parameter.Direction == ParameterDirection.Output ||
					parameter.Direction == ParameterDirection.InputOutput ||
					parameter.Direction == ParameterDirection.ReturnValue
					)
				{
					outputParams[parameter.ParameterName] = Utils.DbNullToClrNull(parameter.Value);
				}
			}

			return outputParams;
		}
	}
}
