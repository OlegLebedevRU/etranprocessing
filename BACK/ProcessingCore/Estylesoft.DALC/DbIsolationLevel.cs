using System;
using System.Data;

namespace Estylesoft.DALC
{
	/// <summary>
	/// ”ровень изол€ции транзакций.
	/// </summary>
	[Flags]
	[Serializable]
	public enum DbIsolationLevel
	{
		/// <summary>
		/// The pending changes from more highly isolated transactions cannot be overwritten.
		/// </summary>
		Chaos,

		/// <summary>
		/// Shared locks are held while the data is being read to avoid dirty reads, but the data can be changed before the end of the transaction, resulting in non-repeatable reads or phantom data.
		/// </summary>
		ReadCommitted,

		/// <summary>
		/// A dirty read is possible, meaning that no shared locks are issued and no exclusive locks are honored.
		/// </summary>
		ReadUncommitted,
		
		/// <summary>
		/// Locks are placed on all data that is used in a query, preventing other users from updating the data. Prevents non-repeatable reads but phantom rows are still possible.
		/// </summary>
		RepeatableRead,

		/// <summary>
		/// A range lock is placed on the DataSet, preventing other users from updating or inserting rows into the dataset until the transaction is complete.
		/// </summary>
		Serializable,

		/// <summary>
		/// A different isolation level than the one specified is being used, but the level cannot be determined.
		/// </summary>
		Unspecified,
	}

	/// <summary>
	/// ¬спомогательный класс, предоставл€ющий методы конвертиции типа значений 
	/// между <see cref="DbIsolationLevel"/> и <see cref="IsolationLevel"/>
	/// </summary>
	public class DbIsolationLevelHelper
	{
		/// <summary>
		/// преобразовать значение типа <see cref="IsolationLevel"/> 
		/// в значение типа в <see cref="DbIsolationLevel"/> 
		/// </summary>
		/// <param name="isolationLevel"></param>
		/// <returns></returns>
		public static DbIsolationLevel FromIsolationLevel(IsolationLevel isolationLevel)
		{
			switch(isolationLevel)
			{
				case IsolationLevel.Chaos:
					return DbIsolationLevel.Chaos;

				case IsolationLevel.ReadCommitted:
					return DbIsolationLevel.ReadCommitted;

				case IsolationLevel.ReadUncommitted:
					return DbIsolationLevel.ReadUncommitted;

				case IsolationLevel.RepeatableRead:
					return DbIsolationLevel.RepeatableRead;

				case IsolationLevel.Serializable:
					return DbIsolationLevel.Serializable;

				case IsolationLevel.Unspecified:
					return DbIsolationLevel.Unspecified;
					
				default:
					return DbIsolationLevel.Unspecified;
			}
		}

		/// <summary>
		/// преобразовать значение типа <see cref="DbIsolationLevel"/> 
		/// в значение типа в <see cref="IsolationLevel"/> 
		/// </summary>
		/// <param name="isolationLevel"></param>
		/// <returns></returns>
		public static IsolationLevel ToIsolationLevel(DbIsolationLevel isolationLevel)
		{
			switch(isolationLevel)
			{
				case DbIsolationLevel.Chaos:
					return IsolationLevel.Chaos;

				case DbIsolationLevel.ReadCommitted:
					return IsolationLevel.ReadCommitted;

				case DbIsolationLevel.ReadUncommitted:
					return IsolationLevel.ReadUncommitted;

				case DbIsolationLevel.RepeatableRead:
					return IsolationLevel.RepeatableRead;

				case DbIsolationLevel.Serializable:
					return IsolationLevel.Serializable;

				case DbIsolationLevel.Unspecified:
					return IsolationLevel.Unspecified;
					
				default:
					return IsolationLevel.Unspecified;
			}
		}
	}
}
