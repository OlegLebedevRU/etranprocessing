using System;
using System.Data;
using System.Collections;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Класс - конструктор объектов <see cref="DbCommand"/>. 
	/// Содержит ряд методов для создания команд. Если в метод были 
	/// переданы значения параметров, то эти значения передаются 
	/// вновь созданой коменде.
	/// </summary>
	/// <remarks>
	/// <para>
	/// 
	/// </para>
	/// <para>
	/// Для этого служит метод PrepareCommand, который сформирует точный 
	/// список параметров и присвоит им значения, которые были сохранены 
	/// во временных параметрах команды.
	/// </para>
	/// </remarks>
	public class DbCommand : IDbCommand
	{
		#region Private constructor

		private DbCommand()
		{}

		#endregion

		#region Stored procedure based commands constructors

		/// <summary>
		/// Метод для конструирования команды на основании имени 
		/// хранимой процедуры и <see cref="Hashtable"/>, содержащей пары 
		/// (имя параметра)/(значение параметра).
		/// </summary>
		/// <param name="spName">Имя хранимой процедуры</param>
		/// <param name="parameterValues">Набор входных параметров</param>
		/// <returns>Объект <see cref="DbCommand"/></returns>
		public static DbCommand SP(string spName, Hashtable parameterValues)
		{
			DbCommand command = SP(spName);
			
			foreach(DictionaryEntry entry in parameterValues)
				command.Parameters.Add(new DbParameter((string)entry.Key, entry.Value));

			return command;
		}


		/// <summary>
		/// Метод для конструирования команды на основании имени 
		/// хранимой процедуры и массива значений параметров.
		/// </summary>
		/// <param name="spName">Имя хранимой процедуры</param>
		/// <param name="parameterValues">Набор входных параметров</param>
		/// <returns>Объект <see cref="DbCommand"/></returns>
		public static DbCommand SP(string spName, params object[] parameterValues)
		{
			DbCommand command = SP(spName);

			int i=0;
			foreach(object val in parameterValues)
			{
				command.Parameters.Add(new DbParameter(string.Format("tmp_param{0}", i++), val));
			}

			return command;
		}

        public static DbCommand SP2(string spName, int timeout, params object[] parameterValues)
        {
            DbCommand command = SP(spName);
            command.CommandTimeout = timeout;

            int i = 0;
            foreach (object val in parameterValues)
            {
                command.Parameters.Add(new DbParameter(string.Format("tmp_param{0}", i++), val));
            }

            return command;
        }
	

		/// <summary>
		/// Метод для конструирования команды на основании имени 
		/// хранимой процедуры.
		/// </summary>
		/// <param name="spName">Имя хранимой процедуры</param>
		/// <returns>Объект <see cref="DbCommand"/></returns>
		public static DbCommand SP(string spName)
		{
			DbCommand command = new DbCommand();
			
			command.CommandText = spName;
			command.CommandType = CommandType.StoredProcedure;

			return command;
		}


		#endregion

		#region Free query based commands constructors

		/// <summary>
		/// Метод для конструирования команды на основании произвольного запроса.
		/// </summary>
		/// <param name="commandText">Произвольный запрос</param>
		/// <returns>Объект <see cref="DbCommand"/></returns>
		public static DbCommand Query(string commandText)
		{
			DbCommand command = new DbCommand();
			
			command.CommandText = commandText;
			command.CommandType = CommandType.Text;
			
			return command;
		}


		#endregion

		#region Real command construction
		
		/// <summary>
		/// Создает экземпляр <see cref="IDbCommand"/>. 
		/// Конкретная реализация команды (SqlCommand, OleDbCommand и т.д.) 
		/// определяется параметром connection.
		/// </summary>
		/// <param name="connection">Соединение с БД, которой будет определяться вид создаваемой команды.</param>
		internal IDbCommand CreateRealCommand(DbConnection connection)
		{
			IDbCommand realCommand = connection.DbProvider.CreateCommand();

			realCommand.CommandType = this.CommandType;
			realCommand.CommandText = this.CommandText;

			if(this._commandTimeout != 0)
				realCommand.CommandTimeout = this._commandTimeout;
	
			if(this.CommandType == CommandType.StoredProcedure)
				CreateRealParameters(realCommand, connection);

			return realCommand;
		}

		/// <summary>
		/// Заполняет список параметров команды и спользуя метод DeriveParameters объекта 
		/// <see cref="IDbProvider"/>, определяемого подключением connection.
		/// </summary>
		/// <param name="realCommand">Экземпляр <see cref="IDbCommand"/>, коллекцию параметров которой нужно заполнить.</param>
		/// <param name="connection">Подключение к БД.</param>
		private void CreateRealParameters(IDbCommand realCommand, DbConnection connection)
		{
			ArrayList existedParameters = new ArrayList();
			
			existedParameters.AddRange(this.Parameters);

			IDataParameter[] realParameters = SqlHelperParameterCache.GetSpParameterSet(connection, realCommand.CommandText);

			realCommand.Parameters.Clear();

			foreach(IDataParameter realParameter in realParameters)
			{
				realCommand.Parameters.Add(realParameter);

				foreach(DbParameter parameter in existedParameters)
				{
					if(parameter.ParameterName == realParameter.ParameterName)
					{
						realParameter.Value = Utils.ClrNullToDbNull(parameter.Value);
						existedParameters.Remove(parameter);
						break;
					}
				}
			}

			if(existedParameters.Count > 0)
			{
				int i=0;
				foreach(IDataParameter parameter in realCommand.Parameters)
				{
					if(parameter.Direction == ParameterDirection.Input || parameter.Direction == ParameterDirection.InputOutput)
					{
						parameter.Value = Utils.ClrNullToDbNull(((DbParameter)existedParameters[i]).Value);
						if(++i >= existedParameters.Count)
							break;
					}
				}
			}		
		}

		#endregion

		#region Implementation of IDbCommand
		
		private CommandType _commandType;
		/// <summary>
		/// Тип команды
		/// </summary>
		public System.Data.CommandType CommandType
		{
			get { return _commandType; }
			set { _commandType = value; }
		}

		private string _commandText;
		/// <summary>
		/// Текст команды или название хранимой процедуры.
		/// </summary>
		public string CommandText
		{
			get { return _commandText; }
			set { _commandText = value; }
		}

		private DbParametersCollection _parameters = new DbParametersCollection();
		/// <summary>
		/// Коллекция параметров команды.
		/// </summary>
		public DbParametersCollection Parameters
		{
			get { return _parameters; }
		}

		IDataParameterCollection IDbCommand.Parameters
		{
			get { return _parameters; }
		}

		private int _commandTimeout = 0;
		/// <summary>
		/// Таймаут. Значение по умолчанию - 0 сек.
		/// </summary>
		/// <remarks>
		/// Если значение твймаута равно 0, то при создании 
		/// реальной команды значение ее таймаута будет оставлено 
		/// без изменения (обычно это 30 секунд).
		/// </remarks>
		public int CommandTimeout
		{
			get { return _commandTimeout; }
			set { _commandTimeout = value; }
		}

		#region Stubs
		
		void IDbCommand.Cancel()
		{
		
		}

		void IDbCommand.Prepare()
		{
		
		}

		System.Data.IDataReader IDbCommand.ExecuteReader(System.Data.CommandBehavior behavior)
		{
			return null;
		}

		System.Data.IDataReader IDbCommand.ExecuteReader()
		{
			return null;
		}

		object IDbCommand.ExecuteScalar()
		{
			return null;
		}

		int IDbCommand.ExecuteNonQuery()
		{
			return 0;
		}

		System.Data.IDbDataParameter IDbCommand.CreateParameter()
		{
			return null;
		}

		int IDbCommand.CommandTimeout
		{
			get { return 0; }
			set { }
		}

		System.Data.IDbConnection IDbCommand.Connection
		{
			get { return null; }
			set { }
		}

		System.Data.UpdateRowSource IDbCommand.UpdatedRowSource
		{
			get { return new UpdateRowSource(); }
			set { }
		}

		System.Data.IDbTransaction IDbCommand.Transaction
		{
			get { return null; }
			set { }
		}

		#endregion

		#endregion	

		#region Implementation of IDisposable

		void IDisposable.Dispose()
		{
		
		}

		#endregion	
	}
}
