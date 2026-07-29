using System;

namespace Estylesoft.DALC.DeclarativeTransaction
{
	/// <summary>
	/// Ѕазовый класс дл€ классов, в которых планируетс€ использовать 
	/// планируетс€ использовать декларативные (основанные на аттрибутах) 
	/// транзакции.	
	/// </summary>
	/// <remarks>
	/// ƒл€ того, что бы методы класса можно было помечать как 
	/// транзакционые, класс должен быть наследником ContextBoundObject 
	/// и иметь аттрибут [TransactionalComponent].
	///  ласс TransactionalComponentBase представл€ет собой абстрактный 
	/// класс, удовлетвор€ющий этим услови€м.
	/// </remarks>
	[DbTransactionalComponent]
	public abstract class TransactionalComponentBase : ContextBoundObject
	{
	}
}
