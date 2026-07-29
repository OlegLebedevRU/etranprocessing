using System;

namespace Estylesoft.DALC
{
	/// <summary>
	///  ласс представл€ет собой исключение, возникающее в Data Access Layer, 
	/// но св€занное с бизнес-логикой работы приложени€.
	/// </summary>
	public class DbBusinessException : DbException
	{
		/// <summary>
		/// »нициализирует новый экземпл€р класса <see cref="DbBusinessException"/>.
		/// </summary>
		/// <param name="message">—ообщение об ошибке.</param>
		public DbBusinessException(string message) : base(message, null)
		{
		}

		/// <summary>
		/// »нициализирует новый экземпл€р класса <see cref="DbBusinessException"/>.
		/// </summary>
		/// <param name="message">—ообщение об ошибке.</param>
		/// <param name="innerException">¬ложенное исключение.</param>
		public DbBusinessException(string message, Exception innerException) : base(message, innerException)
		{
		}

		/// <summary>
		/// »нициализирует новый экземпл€р класса <see cref="DbBusinessException"/>.
		/// </summary>
		/// <param name="innerException">¬ложенное исключение.</param>
		public DbBusinessException(Exception innerException) : base(innerException.Message, innerException)
		{
		}
	}
}
