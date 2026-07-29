using System;
using System.Data.SqlTypes;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Интерфейс, посредством которого <see cref="DbDataReader"/> 
	/// обращается к методам класса-преобразователя данных.
	/// </summary>
	public interface IDbNullDataTransformer
	{
		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа bool.
		/// </summary>
		/// <returns></returns>
		Boolean GetBooleanNullValue();
		
		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа byte.
		/// </summary>
		/// <returns></returns>
		Byte GetByteNullValue();

		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа DateTime.
		/// </summary>
		/// <returns></returns>
		DateTime GetDateTimeNullValue();
		
		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа decimal.
		/// </summary>
		/// <returns></returns>
		Decimal GetDecimalNullValue();
		
		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа double.
		/// </summary>
		/// <returns></returns>
		Double GetDoubleNullValue();
		
		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Guid.
		/// </summary>
		/// <returns></returns>
		Guid GetGuidNullValue();
		
		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Int16.
		/// </summary>
		/// <returns></returns>
		Int16 GetInt16NullValue();
	
		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Int32.
		/// </summary>
		/// <returns></returns>
		Int32 GetInt32NullValue();
	
		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Int64.
		/// </summary>
		/// <returns></returns>
		Int64 GetInt64NullValue();

		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Money.
		/// </summary>
		/// <returns></returns>
		Decimal GetMoneyNullValue();
	
		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Single.
		/// </summary>
		/// <returns></returns>
		Single GetSingleNullValue();

		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа string.
		/// </summary>
		/// <returns></returns>
		String GetStringNullValue();

		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа char.
		/// </summary>
		/// <returns></returns>
		Char GetCharNullValue();

		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа type.
		/// </summary>
		/// <returns></returns>
		Object GetNullValue(Type type);
	}
}
