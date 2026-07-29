using System;
using System.Data;
using System.Collections;
using System.Runtime.Remoting.Messaging;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Класс - мост между участниками транзакции и мененжером 
	/// транзакций (<see cref="TransactionManager"/>).
	/// Реализует поддержку уникальности TransactionManager-а для 
	/// каждого потока приложения.
	/// </summary>
	public class DbManager
	{
		/// <summary>
		/// Строка - ключ, под которым TransactionManager хранится в <see cref="CallContext"/>;
		/// </summary>
		private const string TRANSACTION_MANAGER_KEY = "_TRANSACTION_MANAGER_KEY_";

		/// <summary>
		/// Свойство, показывающее статус (доступность) транзакции. Транзакция 
		/// считается недоступной, если для нее был вызван Rollback. 
		/// </summary>
		public static bool IsTransactionOk
		{
			get 
			{ 
				TransactionManager transactionManager = GetCurrentTransactionManager();
				return transactionManager.IsTransactionOk;
			}
		}

		
		/// <summary>
		/// Инициализирует команду command текущими значениями 
		/// IDbConnection и IDbTransaction.
		/// Используется методами <see cref="DbExecutor"/>.
		/// </summary>
		/// <param name="command"></param>
		/// <param name="connection"></param>
		internal static void InitCommand(IDbCommand command, DbConnection connection)
		{
			TransactionManager transactionManager = GetCurrentTransactionManager();

			DbRealConnection realConnection = transactionManager.GetRealConnection(connection);
			
			command.Connection = (IDbConnection)realConnection.Connection;
			command.Transaction = (IDbTransaction)realConnection.Transaction;
		}


		/// <summary>
		/// Начинает транзакцию и возвращает объект <see cref="TransactionContext"/> 
		/// связанный с текущей транзакцией.
		/// </summary>
		/// <returns></returns>
 		public static TransactionContext BeginTransaction()
		{
			return BeginTransaction(DbTransactionOption.Required);
		}


		/// <summary>
		/// Начинает транзакцию и возвращает объект <see cref="TransactionContext"/> 
		/// связанный с текущей транзакцией.
		/// </summary>
		/// <returns></returns>
		public static TransactionContext BeginTransaction(DbTransactionOption transactionOption)
		{
			TransactionManager transactionManager = GetTransactionManager(transactionOption, DbIsolationLevel.ReadCommitted);

			return transactionManager.BeginTransaction();
		}

		/// <summary>
		/// Начинает транзакцию и возвращает объект <see cref="TransactionContext"/> 
		/// связанный с текущей транзакцией.
		/// </summary>
		/// <returns></returns>
		public static TransactionContext BeginTransaction(DbTransactionOption transactionOption, DbIsolationLevel isolationLevel)
		{
			TransactionManager transactionManager = GetTransactionManager(transactionOption, isolationLevel);

			return transactionManager.BeginTransaction();
		}

		/// <summary>
		/// Начинает транзакцию и возвращает объект <see cref="TransactionContext"/> 
		/// связанный с текущей транзакцией. 
		/// Назначение параметра autoCommit <see cref="TransactionContext"/>
		/// </summary>
		/// <param name="autoCommit">Параметр, определяющих поведение транзакции при ее неявном завершении.</param>
		/// <returns></returns>
		public static TransactionContext BeginTransaction(bool autoCommit)
		{
			TransactionContext transactionContext = BeginTransaction();
			transactionContext.AutoCommit = autoCommit;
			
			return transactionContext;
		}


		/// <summary>
		/// Завершает подтранзакцию, связанную с transactionContext.
		/// </summary>
		/// <param name="transactionContext"></param>
		internal static void CommitTransaction(TransactionContext transactionContext)
		{
			TransactionManager transactionManager = GetCurrentTransactionManager();

			transactionManager.CommitTransaction(transactionContext);

			if(transactionManager.GetSubTransactionsCount() == 0)
				ClearTransactionManager(transactionManager);
		}

		private static void ClearTransactionManager(TransactionManager transactionManager)
		{
			Stack transactionManagersStack;

			object contextObject = CallContext.GetData(TRANSACTION_MANAGER_KEY);

			if(contextObject is Stack)
			{
				transactionManagersStack = (Stack)contextObject;
				if(transactionManagersStack.Count > 0)
				{
					if(transactionManager.Equals(transactionManagersStack.Peek()))
						transactionManagersStack.Pop();
				}
			}
		}

		/// <summary>
		/// Отменяет подтранзакцию, связанную с transactionContext.
		/// </summary>
		/// <param name="transactionContext"></param>
		internal static void RollbackTransaction(TransactionContext transactionContext)
		{
			TransactionManager transactionManager = GetCurrentTransactionManager();

			transactionManager.RollbackTransaction(transactionContext);

			if(transactionManager.GetSubTransactionsCount() == 0)
				ClearTransactionManager(transactionManager);
		}

		private static Stack GetTransactionManagers()
		{
			Stack transactionManagers;

			object contextObject = CallContext.GetData(TRANSACTION_MANAGER_KEY);

			if(contextObject is Stack)
				transactionManagers = (Stack)contextObject;
			else
			{
				transactionManagers = new Stack();
				CallContext.SetData(TRANSACTION_MANAGER_KEY, transactionManagers);
			}

			return transactionManagers;
		}
		

		private static TransactionManager GetCurrentTransactionManager()
		{
			Stack transactionManagersStack = GetTransactionManagers();

			TransactionManager transactionManager;

			if(transactionManagersStack.Count == 0)
			{
				transactionManager = new TransactionManager();
				transactionManager.TransactionOption = DbTransactionOption.Supported;
				transactionManagersStack.Push(transactionManager);
			}
			else
			{
				transactionManager = (TransactionManager)transactionManagersStack.Peek();
			}

			return transactionManager;
		}

		/// <summary>
		/// Возвращает <see cref="TransactionManager"/> для текущего потока.
		/// </summary>
		/// <returns><see cref="TransactionManager"/> текущего потока.</returns>
		private static TransactionManager GetTransactionManager(DbTransactionOption transactionOption, DbIsolationLevel isolationLevel)
		{
			TransactionManager transactionManager;

			transactionManager = GetCurrentTransactionManager();

			if(transactionManager.IsNewManagerRequired(transactionOption))
			{
				Stack transactionManagers = GetTransactionManagers();

				transactionManager = new TransactionManager();
				transactionManager.TransactionOption = transactionOption;
				transactionManagers.Push(transactionManager);
			}

			transactionManager.IsolationLevel = isolationLevel;
			
			return transactionManager;
		}
	}
}
