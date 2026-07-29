using System;
using System.Data.SqlClient;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Реализация <see cref="IDbExceptionProvider"/> 
	/// для <see cref="System.Data.SqlClient"/>.
	/// </summary>
	/// <remarks>
	/// Если номер ошибки равен или превышает значение константы 
	/// <see cref="USER_EXECEPTION_MIN_NUMBER"/>, то входящее исключение 
	/// преобразуется в исключение типа <see cref="DbBusinessException"/>, 
	/// иначе - в исключение типа <see cref="DbException"/>.
	/// </remarks>
	public class SqlExceptionProvider : IDbExceptionProvider
	{
		/// <summary>
		/// Константа равная минимальному номеру пользвателькой (не системной) ошибки.
		/// </summary>
		private const int USER_EXECEPTION_MIN_NUMBER = 50000;
	
		#region Implementation of IDbExceptionProvider

		/// <summary>
		/// Преобразовать исключение, специфичное для провайдера БД в 
		/// исключение типа DbException или производное от него.
		/// </summary>
		public System.Exception TransformException(System.Exception exception)
		{
			if(exception is SqlException)
			{
				SqlException sqlException = (SqlException)exception;

				if(sqlException.Number >= USER_EXECEPTION_MIN_NUMBER)
					return new DbBusinessException(sqlException);
				else
					return new DbException(sqlException);
			}
			else
				return exception;
		}

		#endregion
	}
}
