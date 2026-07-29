using System;
using System.Runtime.Remoting.Messaging;

namespace Estylesoft.DALC.DeclarativeTransaction
{
	/// <summary>
	/// Реализация приемника объекта. Основная задача 
	/// приемника это выполнить вызов напрямую или 
	/// выполнить его в рамках транзакции. 
	/// </summary>
	internal class TransactionMessageSink : IMessageSink
	{  
		private IMessageSink		_nextSink;
		private ContextBoundObject	_context;
    
		public TransactionMessageSink(ContextBoundObject instance, IMessageSink nextSink)
		{
			_nextSink = nextSink;
			_context = instance;
		}

		public IMessageCtrl AsyncProcessMessage(IMessage msg, IMessageSink replySink)
		{
			IMessage retMsg = _nextSink.SyncProcessMessage(msg);

			if (replySink != null) 
			{
				replySink.SyncProcessMessage(retMsg);
			}

			return null;
		}

		/// <summary>
		/// Обработка сообщения. 
		/// </summary>
		public IMessage SyncProcessMessage(IMessage msg)
		{
			// Perform whatever preprocessing is needed on the message
			if (!(msg is IMethodMessage))
				return _nextSink.SyncProcessMessage(msg);

			IMethodMessage imm = msg as IMethodMessage;

			DbTransactionAttribute transactionAttribute = (DbTransactionAttribute)Attribute.GetCustomAttribute(imm.MethodBase, typeof(DbTransactionAttribute));
			
			if(transactionAttribute == null)
				return _nextSink.SyncProcessMessage(msg);

			IMessage imReturn = null;
			
			using(TransactionContext tc = DbManager.BeginTransaction(transactionAttribute.TransactionOption, transactionAttribute.IsolationLevel))
			{
				imReturn = _nextSink.SyncProcessMessage(msg);

				IMethodReturnMessage methodReturn = imReturn as IMethodReturnMessage;
				Exception exc = methodReturn.Exception;
				if (exc != null)
				{
					tc.Rollback();
				}
				else
				{
					tc.Commit();
				}
			}
			
			return imReturn;
		}
  
		public IMessageSink NextSink
		{
			get { return _nextSink; }
		}
	}
}
