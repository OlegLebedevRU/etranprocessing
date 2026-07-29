using System;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Класс-преобразовательданных, в котором реализован следующий
	/// алгоритм: если значение поля равно DbNull.Value, то оно
	/// заменяется на значение соответствующеего свойства.
	/// <para>
	/// (bool)value заменяется на значение BooleanNullReplacement
	/// </para>
	/// <para>
	/// (DateTime)value заменяется на значение DateTimeNullReplacement
	/// </para>
	/// <para>
	/// и т.д.
	/// </para>
	/// </summary>
	public class DbNullDataTransformer : IDbNullDataTransformer
	{
		/// <summary>
		/// Инициализирует новый объект класса <see cref="DbNullDataTransformer"/>.
		/// </summary>
		public DbNullDataTransformer()
		{
			_booleanNullReplacement = false;
			_byteNullReplacement = 0;
			_dateTimeNullReplacement = DateTime.MinValue;
			_decimalNullReplacement = 0;
			_doubleNullReplacement = 0;
			_guidNullReplacement = Guid.Empty;
			_int16NullReplacement = 0;
			_int32NullReplacement = 0;
			_int64NullReplacement = 0;
			_moneyNullReplacement = 0;
			_singleNullReplacement = 0;
			_stringNullReplacement = String.Empty;
		}


		#region Transformation map settings

		private bool _booleanNullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа bool.
		/// </summary>
		public bool BooleanNullReplacement
		{
			get { return _booleanNullReplacement; }
			set { _booleanNullReplacement = value; }
		}
	
		private byte _byteNullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа byte.
		/// </summary>
		public byte ByteNullReplacement
		{
			get { return _byteNullReplacement; }
			set { _byteNullReplacement = value; }
		}

		private DateTime _dateTimeNullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа DateTime.
		/// </summary>
		public DateTime DateTimeNullReplacement
		{
			get { return _dateTimeNullReplacement; }
			set { _dateTimeNullReplacement = value; }
		}

		private decimal _decimalNullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа decimal.
		/// </summary>
		public decimal DecimalNullReplacement
		{
			get { return _decimalNullReplacement; }
			set { _decimalNullReplacement = value; }
		}
	
		private double _doubleNullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа double.
		/// </summary>
		public double DoubleNullReplacement
		{
			get { return _doubleNullReplacement; }
			set { _doubleNullReplacement = value; }
		}

		private Guid _guidNullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа Guid.
		/// </summary>
		public Guid GuidNullReplacement
		{
			get { return _guidNullReplacement; }
			set { _guidNullReplacement = value; }
		}

		private Int16 _int16NullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа Int16.
		/// </summary>
		public Int16 Int16NullReplacement
		{
			get { return _int16NullReplacement; }
			set { _int16NullReplacement = value; }
		}

		private Int32 _int32NullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа Int32.
		/// </summary>
		public Int32 Int32NullReplacement
		{
			get { return _int32NullReplacement; }
			set { _int32NullReplacement = value; }
		}

		private Int64 _int64NullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа Int64.
		/// </summary>
		public Int64 Int64NullReplacement
		{
			get { return _int64NullReplacement; }
			set { _int64NullReplacement = value; }
		}

		private Decimal _moneyNullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа Money.
		/// </summary>
		public Decimal MoneyNullReplacement
		{
			get { return _moneyNullReplacement; }
			set { _moneyNullReplacement = value; }
		}

		private float _singleNullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа float.
		/// </summary>
		public float SingleNullReplacement
		{
			get { return _singleNullReplacement; }
			set { _singleNullReplacement = value; }
		}

		private string _stringNullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа string.
		/// </summary>
		public string StringNullReplacement
		{
			get { return _stringNullReplacement; }
			set { _stringNullReplacement = value; }
		}

		private char _charNullReplacement;
		/// <summary>
		/// Значение, на которое будет заменяться значение null 
		/// для переменных типа char.
		/// </summary>
		public char CharNullReplacement
		{
			get { return _charNullReplacement; }
			set { _charNullReplacement = value; }
		}


		#endregion

		#region IDbNullDataTransformer implementation

		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа bool.
		/// </summary>
		/// <returns></returns>
		public bool GetBooleanNullValue()
		{
			return _booleanNullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа byte.
		/// </summary>
		/// <returns></returns>
		public byte GetByteNullValue()
		{
			return _byteNullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа DateTime.
		/// </summary>
		/// <returns></returns>
		public System.DateTime GetDateTimeNullValue()
		{
			return _dateTimeNullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа decimal.
		/// </summary>
		/// <returns></returns>
		public decimal GetDecimalNullValue()
		{
			return _decimalNullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа double.
		/// </summary>
		/// <returns></returns>
		public double GetDoubleNullValue()
		{
			return _doubleNullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Guid.
		/// </summary>
		/// <returns></returns>
		public System.Guid GetGuidNullValue()
		{
			return _guidNullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Int16.
		/// </summary>
		/// <returns></returns>
		public short GetInt16NullValue()
		{
			return _int16NullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Int32.
		/// </summary>
		/// <returns></returns>
		public int GetInt32NullValue()
		{
			return _int32NullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Int64.
		/// </summary>
		/// <returns></returns>
		public long GetInt64NullValue()
		{
			return _int64NullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Money.
		/// </summary>
		/// <returns></returns>
		public decimal GetMoneyNullValue()
		{
			return _decimalNullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа Single.
		/// </summary>
		/// <returns></returns>
		public float GetSingleNullValue()
		{
			return _singleNullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа string.
		/// </summary>
		/// <returns></returns>
		public string GetStringNullValue()
		{
			return _stringNullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа char.
		/// </summary>
		/// <returns></returns>
		public char GetCharNullValue()
		{
			return _charNullReplacement;
		}


		/// <summary>
		/// Получить значение, на которое следует заменить 
		/// значение null для переменных типа type.
		/// </summary>
		/// <returns></returns>
		public Object GetNullValue(Type type)
		{
			if(type == typeof(Boolean))
				return GetBooleanNullValue();
			else if(type == typeof(Byte))
				return GetByteNullValue();
			else if(type == typeof(DateTime))
				return GetDateTimeNullValue();
			else if(type == typeof(Decimal))
				return GetDecimalNullValue();
			else if(type == typeof(Double))
				return GetDoubleNullValue();
			else if(type == typeof(Guid))
				return GetGuidNullValue();
			else if(type == typeof(Int16))
				return GetInt16NullValue();
			else if(type == typeof(Int32))
				return GetInt32NullValue();
			else if(type == typeof(Int64))
				return GetInt64NullValue();
			else if(type == typeof(Single))
				return GetSingleNullValue();
			else if(type == typeof(String))
				return GetStringNullValue();
			else if(type == typeof(Char))
				return GetCharNullValue();
			else
				return null;
		}


		#endregion
	
	}
}
