using System;
using System.Runtime.Remoting.Contexts;
using System.Runtime.Remoting.Activation;
using System.Runtime.Remoting.Messaging;

namespace Estylesoft.DALC.DeclarativeTransaction
{
	/// <summary>
	/// Ётим аттрибутом должен быть помечен класс, в котором
	/// планируетс€ использовать декларативные (основанные на 
	/// аттрибутах) транзакции.
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public class DbTransactionalComponentAttribute : ContextAttribute, IContributeObjectSink
	{
		/// <summary>
		/// »нициализирует новый экземпл€р класса <see cref="DbTransactionalComponentAttribute"/>.
		/// </summary>
		public DbTransactionalComponentAttribute() : base ("TransactionalComponent")
		{    
		}

		/// <summary>
		/// </summary>
		/// <param name="ctx"></param>
		/// <param name="ctorMsg"></param>
		/// <returns></returns>
		public override bool IsContextOK(Context ctx, IConstructionCallMessage ctorMsg)
		{
			return false;
		}
  
		/// <summary>
		/// </summary>
		/// <param name="obj"></param>
		/// <param name="nextSink"></param>
		/// <returns></returns>
		public IMessageSink GetObjectSink(MarshalByRefObject obj, IMessageSink nextSink)
		{
			return new TransactionMessageSink((ContextBoundObject)obj, nextSink);
		}
	}
}
