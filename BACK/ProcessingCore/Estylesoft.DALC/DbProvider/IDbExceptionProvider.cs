using System;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Этот интерфейс используется для преобразования исключений, 
	/// специфичных для провайдера БД в исключение типа DbException или 
	/// или производное от него.
	/// </summary>
	public interface IDbExceptionProvider
	{
		/// <summary>
		/// Преобразовать исключение, специфичное для провайдера БД в 
		/// исключение типа DbException или производное от него.
		/// </summary>
		System.Exception TransformException(System.Exception exception);
	}
}
