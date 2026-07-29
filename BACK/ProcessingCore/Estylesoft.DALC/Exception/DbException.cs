using System;

namespace Estylesoft.DALC
{
	/// <summary>
	///  ласс представл€ет собой исключение, св€занное с ошибками 
	/// Data Access Layer.
	/// </summary>
	public class DbException : Exception
	{
		/// <summary>
		/// »нициализирует новый экземпл€р класса <see cref="DbException"/>
		/// сообщением об ошибке <paramref name="message"/>.
		/// </summary>
		/// <param name="message">—ообщение об ошибке.</param>
		public DbException(string message) : base(message, null)
		{
		}

		/// <summary>
		/// »нициализирует новый экземпл€р класса <see cref="DbException"/>.
		/// </summary>
		/// <param name="message">—ообщение об ошибке.</param>
		/// <param name="innerException">¬ложенное исключение.</param>
		public DbException(string message, Exception innerException) : base(message, innerException)
		{
		}

		/// <summary>
		/// »нициализирует новый экземпл€р класса <see cref="DbException"/>.
		/// </summary>
		/// <param name="innerException">¬ложенное исключение.</param>
		public DbException(Exception innerException) : base(innerException.Message, innerException)
		{
		}
	}
}
