using System;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Класс, связанный с текущей транзакцияей, и позволяющий ее 
	/// принять или отменить.
	/// </summary>
	/// <remarks>
	/// Класс является наследником <see cref="IDisposable"/>. Поведение 
	/// транзакции в случае, если на момент Dispose() ни один из методов 
	/// (Commit или Rollback) не был вызван, определяется значением 
	/// свойства AutoCommit. Подробнее <see cref="AutoCommit"/>
	/// </remarks>
	public class TransactionContext : IDisposable
	{
		private bool _processed = false;
		private bool _autoCommit = false;

		/// <summary>
		/// Параметр AutoCommit определяет поведение транзакции при 
		/// ее неявном закрытии.
		/// </summary>
		/// <remarks>
		/// Если <c>AutoCommit == true</c>, то транзакция 
		/// будет атоматически принята. По умолчанию значение этого параметра 
		/// равно false.
		/// <code>
		/// using(TransactionContext tc = TransactionManager.BeginTransaction(false)) // Создается TransactionContext со значением AutoCommit == false
		/// {
		///		Executor.ExecuteNonQuery(DbCommand.SP("sp_create_card", 0));
		///		Executor.ExecuteNonQuery(DbCommand.SP("sp_create_card", 1));
		///		
		///		tc.Commit();	// Если закомментировать эту строку, то транзакция будет 
		///						// автоматически отменена после выхода из блока using
		/// }
		/// </code>		
		/// </remarks>
		public bool AutoCommit
		{
			get { return _autoCommit; }
			set { _autoCommit = value; }
		}
		

		/// <summary>
		/// Конструктор TransactionContext.
		/// </summary>
		internal TransactionContext()
		{
		}


		/// <summary>
		/// Принять транзакцию
		/// </summary>
		public void Commit()
		{
			DbManager.CommitTransaction(this);
			_processed = true;
		}


		/// <summary>
		/// Отменить транзакцию
		/// </summary>
		public void Rollback()
		{
			DbManager.RollbackTransaction(this);
			_processed = true;
		}


		void IDisposable.Dispose()
		{
			if(!_processed)
			{
				if(_autoCommit)
					Commit();
				else
					Rollback();
			}
		}
	}
}
