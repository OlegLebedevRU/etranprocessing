using System;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Класс представляет собой исключение, возникающее в Data Access Layer, 
	/// и связанное с обработко транзакций.
	/// </summary>
	public class DbTransactionManagerException : DbException
	{
		private DbTransactionManagerExceptionCode _code;

		/// <summary>
		/// Код ошибки.
		/// </summary>
		public DbTransactionManagerExceptionCode Code
		{
			get { return _code; }
		}

		/// <summary>
		/// Допустимые коды ошибок.
		/// </summary>
		public enum DbTransactionManagerExceptionCode
		{
			/// <summary>
			/// Транзакция была прервана.
			/// </summary>
			TransactionAborted,

			/// <summary>
			/// Отсутствуют открытые транзакции.
			/// </summary>
			NoOpenTransactions,
			
			/// <summary>
			/// Контекст транзакции не найден.
			/// </summary>
			UnknownTransactionContext,

			/// <summary>
			/// Попытка выполнить перекрестную транзакцию.
			/// </summary>
			CrossTransaction,
		}

		private const string TRANSACTION_ABORTED_ERROR_MESSAGE = "Current transaction was canceled (rollbacked).";
		private const string NO_OPEN_TRANSACTIONS_ERROR_MESSAGE = "No open transactions.";
		private const string UNKNOWN_TRANSACTION_CONTEXT_ERROR_MESSAGE = "Unknown transaction context.";
		private const string CROSS_TRANSACTION_ERROR_MESSAGE = "Cross-transactions is not supported.";

		private DbTransactionManagerException(string message) : base(message) 
		{}

		/// <summary>
		/// Сгенерировать исключение указанного типа.
		/// </summary>
		/// <param name="code">Код ошибки.</param>
		internal static void ThrowException(DbTransactionManagerExceptionCode code)
		{
			string message = "";

			switch(code)
			{
				case DbTransactionManagerExceptionCode.TransactionAborted:
					message = TRANSACTION_ABORTED_ERROR_MESSAGE;
					break;

				case DbTransactionManagerExceptionCode.NoOpenTransactions:
					message = NO_OPEN_TRANSACTIONS_ERROR_MESSAGE;
					break;

				case DbTransactionManagerExceptionCode.UnknownTransactionContext:
					message = UNKNOWN_TRANSACTION_CONTEXT_ERROR_MESSAGE;
					break;

				case DbTransactionManagerExceptionCode.CrossTransaction:
					message = CROSS_TRANSACTION_ERROR_MESSAGE;
					break;

				default:
					message = "Unknown error";
					break;
			}

			DbTransactionManagerException exception = new DbTransactionManagerException(message);
			
			exception._code = code;

			throw exception;
		}
	}
}
