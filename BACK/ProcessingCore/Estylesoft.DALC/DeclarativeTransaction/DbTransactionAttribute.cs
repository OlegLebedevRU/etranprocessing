using System;
using System.Data;

using Estylesoft.DALC;

namespace Estylesoft.DALC.DeclarativeTransaction
{
	/// <summary>
	/// Ётим аттрибутом должен быть помечен метод, 
	/// который должен выполн€тьс€ в транзакции. 
	/// </summary>
	[AttributeUsage(AttributeTargets.Method)]
	public class DbTransactionAttribute : Attribute
	{
		private DbIsolationLevel _isolationLevel;
		private DbTransactionOption _transactionOption;
		
		/// <summary>
		/// »нициализирует новый экземпл€р класса <see cref="DbTransactionAttribute"/>.
		/// </summary>
		/// <remarks>
		/// «начени€ свойств нового объекта:
		///	IsolationLevel = DbIsolationLevel.ReadCommitted;
		///	TransactionOption = DbTransactionOption.Required;
		/// </remarks>
		public DbTransactionAttribute()
		{
			_isolationLevel = DbIsolationLevel.ReadCommitted;
			_transactionOption = DbTransactionOption.Required;
		}

		/// <summary>
		/// »нициализирует новый экземпл€р класса <see cref="DbTransactionAttribute"/>.
		/// </summary>
		/// <remarks>
		/// «начени€ свойств нового объекта:
		///	IsolationLevel = DbIsolationLevel.ReadCommitted;
		///	TransactionOption = transactionOption;
		/// </remarks>
		public DbTransactionAttribute(DbTransactionOption transactionOption)
		{
			_transactionOption = transactionOption;
			_isolationLevel = DbIsolationLevel.ReadCommitted;
		}
		
		/// <summary>
		/// »нициализирует новый экземпл€р класса <see cref="DbTransactionAttribute"/>.
		/// </summary>
		/// <remarks>
		/// «начени€ свойств нового объекта:
		///	IsolationLevel = isolationLevel;
		///	TransactionOption = DbTransactionOption.Required;
		/// </remarks>
		public DbTransactionAttribute(DbIsolationLevel isolationLevel)
		{
			_transactionOption = DbTransactionOption.Required;
			_isolationLevel = isolationLevel;
		}

		/// <summary>
		/// »нициализирует новый экземпл€р класса <see cref="DbTransactionAttribute"/>.
		/// </summary>
		/// <remarks>
		/// «начени€ свойств нового объекта:
		///	IsolationLevel = isolationLevel;
		///	TransactionOption = transactionOption;
		/// </remarks>
		public DbTransactionAttribute(DbTransactionOption transactionOption, DbIsolationLevel isolationLevel)
		{
			_transactionOption = transactionOption;
			_isolationLevel = isolationLevel;
		}
		
		/// <summary>
		/// ”ровень изол€ции транзакций.
		/// </summary>
		public DbIsolationLevel IsolationLevel
		{
			get { return _isolationLevel; }
			set { _isolationLevel = value; }
		}

		/// <summary>
		/// “ип требуемой транзакции.
		/// </summary>
		public DbTransactionOption TransactionOption
		{
			get { return _transactionOption; }
			set { _transactionOption = value; }
		}
	}
}
