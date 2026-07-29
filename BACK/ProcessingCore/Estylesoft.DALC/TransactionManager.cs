using System;
using System.Data;
using System.Collections;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Менеджер транзакций. 
	/// </summary>
	internal class TransactionManager
	{
		private bool UseTransaction
		{
			get { return (_transactionOption == DbTransactionOption.Required || _transactionOption == DbTransactionOption.RequiresNew); }
		}

		bool _isTransactionOk = true;
		
		/// <summary>
		/// Флаг IsTransactionOk сигнализирует о том, что транзакция будет 
		/// отменена. Все операции с БД (в рамках этой транзакции) при 
		/// IsTransactionOk == false будут вызывать exception.
		/// </summary>
		/// <remarks>
		/// <para>
		/// Это свойство устанавливается в <c>false</c> когда хотя бы для 
		/// одной из подтранзакций будет вызван Rollback. 
		/// </para>
		/// <para>
		/// Свойство IsTransactionOk устанавливается в <c>true</c> после того,
		/// как все подранзакции будут завершены.
		/// </para>
		/// </remarks>
		internal bool IsTransactionOk
		{
			get { return _isTransactionOk; }
		}

		private DbTransactionOption _transactionOption = DbTransactionOption.Supported;

		public DbTransactionOption TransactionOption
		{
			get { return _transactionOption; }
			set { _transactionOption = value; }
		}


		private DbIsolationLevel _isolationLevel = DbIsolationLevel.ReadCommitted;

		public DbIsolationLevel IsolationLevel
		{
			get { return _isolationLevel; }
			set { _isolationLevel = value; }
		}


		/// <summary>
		/// Стек вложенных подтранзакций.
		/// </summary>
		private Stack transactionStack = new Stack();

		/// <summary>
		/// Коллекция подключений к БД, используемых в рамках текущей транзакции.
		/// </summary>
		private Hashtable transactionConnections = new Hashtable();
		
		/// <summary>
		/// Возвращает объект <see cref="DbRealConnection"/> для указанной строки 
		/// подключения. Используется методами <see cref="DbManager"/>.
		/// </summary>
		/// <remarks>
		/// Если вызов метода происходит вне транзакции, то просто создается новый 
		/// объект <see cref="DbRealConnection"/>.
		/// <para>
		/// Если метод обнаруживает, что выполняется в транзакции, 
		/// то производит поиск коннекта с указаной строкой в <see cref="transactionConnections"/>
		/// и возвращает найденое подключение. В случае неудачного поиска создается 
		/// новое подключение, в котором открывается транзакция. Новое подключение 
		/// заносится в коллекцию <see cref="transactionConnections"/> и возвращается.
		/// </para>
		/// </remarks>
		/// <param name="connection"></param>
		/// <returns><see cref="DbRealConnection"/></returns>
		internal DbRealConnection GetRealConnection(DbConnection connection)
		{
			DbRealConnection realConnection;

			string connectionKey = string.Format("{0}#{1}", connection.ConnectionString, connection.DbProvider.ProviderName);
			
			if(UseTransaction)
			{
				if(transactionConnections.Contains(connectionKey))
					realConnection = ((DbRealConnection)transactionConnections[connectionKey]);
				else
				{
					realConnection = new DbRealConnection(connection);
					realConnection.BeginTransaction(_isolationLevel);
					transactionConnections.Add(connectionKey, realConnection);
				}
			}
			else
			{
				realConnection = new DbRealConnection(connection);
			}

			return realConnection;
		}


		internal long GetSubTransactionsCount()
		{
			return transactionStack.Count;
		}

		
		/// <summary>
		/// Завершает подтранзакцию, связанную с переданным объектом transactionContext.
		/// </summary>
		/// <remarks>
		/// Транзакция извлекается из стека текущих подтранзакций <see cref="transactionStack"/>,
		/// и если оказывется, что стек освободился, то выполняется Commit во 
		/// всех коннектах, участвоваших в транзакции. После этого все коннекты 
		/// закрываются и удаляются из коллекции <see cref="transactionConnections"/>.
		/// </remarks>
		/// <param name="transactionContext"></param>
		internal void CommitTransaction(TransactionContext transactionContext)
		{
			CheckForValidContext(transactionContext);

			// Извлечем из стека текущую подтранзакцию, включая все вложенные
			while(transactionStack.Count > 0 && !transactionContext.Equals(transactionStack.Pop()));
			
			// Попытка сделать commit после rollback-а. 
			// Ситуация возникает, если для вложенной транзакции был выполнен Rollback()
			if(!_isTransactionOk)
				DbTransactionManagerException.ThrowException(DbTransactionManagerException.DbTransactionManagerExceptionCode.TransactionAborted);

			if(transactionStack.Count == 0)
			{
				foreach(DbRealConnection realConnection in transactionConnections.Values)
				{
					realConnection.Transaction.Commit();
					realConnection.Connection.Close();
				}

				transactionConnections.Clear();
			}
		}


		/// <summary>
		/// Отменяет подтранзакцию, связанную с переданным объектом transactionContext.
		/// </summary>
		/// <remarks>
		/// Транзакция извлекается из стека текущих подтранзакций <see cref="transactionStack"/>,
		/// и выполняется Rollback во всех коннектах, участвоваших в транзакции. 
		/// После этого все коннекты закрываются и удаляются из коллекции <see cref="transactionConnections"/>.
		/// Транзакция помечается как failed, для того, что бы последующие
		/// действия в рамках этой транзакции были невозможны.
		/// <para>
		/// Статус транзакции failed снимается после того, как будут обработаны 
		/// все все транзакции, т.е. <see cref="transactionStack"/> окажется пустым.
		/// </para>
		/// </remarks>
		/// <param name="transactionContext"></param>
		internal void RollbackTransaction(TransactionContext transactionContext)
		{
			CheckForValidContext(transactionContext);

			// Извлечем из стека текущую подтранзакцию, включая все вложенные
			while(transactionStack.Count > 0 && !transactionContext.Equals(transactionStack.Pop()));
			
			foreach(DbRealConnection realConnection in transactionConnections.Values)
			{
				try
				{
					realConnection.Transaction.Rollback();
				}
				catch(Exception e)
				{
					if(e is System.Data.SqlClient.SqlException)
					{
						if(((System.Data.SqlClient.SqlException)e).Number != 3903)
							throw e;
					}
					else
						throw e;
				}
				realConnection.Connection.Close();
			}

			transactionConnections.Clear();

			if(transactionStack.Count > 0)
			{
				// см. IsTransactionOk
				_isTransactionOk = false;
			}
			else
			{
				transactionStack.Clear();
				_isTransactionOk = true;
			}
		}


		/// <summary>
		/// Начинает подтранзакцию. 
		/// </summary>
		/// <returns></returns>
		internal TransactionContext BeginTransaction()
		{
			TransactionContext transactionContext = new TransactionContext();

			transactionStack.Push(transactionContext);

			return transactionContext;
		}
		

		/// <summary>
		/// Некоторые проверки контекста транзакции, которые будут выполняться 
		/// только в DEBUG версии.
		/// <para>Список проверок:</para>
		/// <list type="bullet">
		///	<item>
		///	Попытка обработать транзакцию, когда все транзакции уже обработаны.
		///	</item>
		///	<item>
		///	Попытка обработать транзакцию, которая уже была обработана.
		///	</item>
		///	<item>
		///	Попытка обработать транзакцию, когда предыдущая транзакция еще не завершена.
		/// Например, при использовании транзакций без конструкции using(...)
		///	</item>
		/// </list>
		/// </summary>
		/// <param name="transactionContext"><see cref="TransactionContext"/></param>
		private void CheckForValidContext(TransactionContext transactionContext)
		{
#if DEBUG
			// Попытка обработать транзакцию, когда все транзакции уже обработаны.
			if(transactionStack.Count == 0)
				DbTransactionManagerException.ThrowException(DbTransactionManagerException.DbTransactionManagerExceptionCode.NoOpenTransactions);

			// Попытка обработать транзакцию, которая уже была обработана.
			if(!transactionStack.Contains(transactionContext))
				DbTransactionManagerException.ThrowException(DbTransactionManagerException.DbTransactionManagerExceptionCode.UnknownTransactionContext);
			
			// Попытка обработать транзакцию, когда предыдущая транзакция еще не завершена. 
			// Например, при использовании транзакций без конструкции using(...)
			if(!transactionContext.Equals(transactionStack.Peek()))
				DbTransactionManagerException.ThrowException(DbTransactionManagerException.DbTransactionManagerExceptionCode.CrossTransaction);

#endif
		}

		internal bool IsNewManagerRequired(DbTransactionOption transactionOption)
		{
			switch(transactionOption)
			{
				case DbTransactionOption.RequiresNew:
					return true;

				case DbTransactionOption.Required:
					return !UseTransaction;

				case DbTransactionOption.Supported:
					return false;

				case DbTransactionOption.Disabled:
				case DbTransactionOption.NotSupported:
					return UseTransaction;

				default:
					return true;
			}
		}
	}
}
