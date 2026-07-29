using System;
using System.Collections;
using System.Data;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Класс - обертка над <see cref="IDataReader"/>-ом, позволяющий 
	/// осуществлять "безопасное" чтение значений полей. В процессе чтения
	/// автоматически заменяет DbNull значения полей использую алгоритм, 
	/// предоставленный <see cref="IDbNullDataTransformer"/>. 
	/// Используемая реализация <see cref="IDbNullDataTransformer"/> 
	/// определяется свойством <see cref="NullDataTransformer"/>.
	/// </summary>
	public class DbDataReader : IDataReader, IDataRecord, IEnumerable, IDisposable
	{
		/// <summary>
		/// Внутренне поле, содержащее оригинальный SqlDataReader.
		/// </summary>
		private IDataReader _dataReader = null;

		/// <summary>
		/// Внутреннее поле, содержащее ссылку на используемый IDbDataTransformer.
		/// По умолчанию инициализируется новым объектом класса DbDataTransformer.
		/// </summary>
		private IDbNullDataTransformer _nullTransformer = new DbNullDataTransformer();

		/// <summary>
		/// Хэш-таблица пар [имя поля] -> [порядок поля] предназначена
		/// для быстрого доступа к полям по имени.
		/// </summary>
		private Hashtable _fieldNameLookup = System.Collections.Specialized.CollectionsUtil.CreateCaseInsensitiveHashtable();
		
		/// <summary>
		/// Создание DbDataReader-а и инициализация его IDataReader-ом.
		/// </summary>
		/// <param name="dataReader"></param>
		public DbDataReader(IDataReader dataReader)
		{
			_dataReader = dataReader;
		}


		/// <summary>
		/// Свойство, позволяющее получить или установить DataTransformer.
		/// Если установить зачение <c>DataTransformer = null</c>, то 
		/// трансформации происходить не будет.
		/// </summary>
		public IDbNullDataTransformer NullDataTransformer
		{
			get { return _nullTransformer; }
			set { _nullTransformer = value; }
		}


		/// <summary>
		/// Read only свойство для доступа к оригинальному DataReader-у.
		/// </summary>
		public IDataReader UnderlyingDataReader
		{
			get { return _dataReader; }
		}


		#region IDataReader implementation

		/// <summary>
		/// Закрывает объект IDataReader.
		/// </summary>
		public void Close()
		{
			_dataReader.Close();
		}

		/// <summary>
		/// Возвращает объект DataTable, описывающий 
		/// метаданные о столбцах объекта IDataReader.
		/// </summary>
		/// <returns>Объект DataTable, описывающий метаданные о столбцах.</returns>
		public System.Data.DataTable GetSchemaTable()
		{
			return _dataReader.GetSchemaTable();
		}

		/// <summary>
		/// Перемещает позицию объекта DataReader на следующий набор 
		/// результатов, если происходит чтение результатов пакета 
		/// инструкций SQL.
		/// </summary>
		/// <returns>true, если есть еще строки, false в противном случае.</returns>
		public bool NextResult()
		{
			return _dataReader.NextResult();
		}

		/// <summary>
		/// Перемещает позицию объекта IDataReader на следующую запись.
		/// </summary>
		/// <returns>true, если есть еще строки, false в противном случае.</returns>
		public bool Read()
		{
			return _dataReader.Read();
		}

		/// <summary>
		/// Получает значение, показывающее глубину вложенности текущей строки.
		/// </summary>
		public int Depth
		{
			get
			{
				return _dataReader.Depth;
			}
		}

		/// <summary>
		/// Возвращает значение, определяющее, закрыт ли объект DataReader.
		/// </summary>
		public bool IsClosed
		{
			get
			{
				return _dataReader.IsClosed;
			}
		}

		/// <summary>
		/// Получает число строк, измененных, вставленных или удаленных при выполнении инструкции SQL.
		/// </summary>
		public int RecordsAffected
		{
			get
			{
				return _dataReader.RecordsAffected;
			}
		}
		#endregion

		#region IDataRecord implementation
		
		/// <summary>
		/// Получает значение указанного столбца в качестве логического значения.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public bool GetBoolean(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetBoolean(i);
			else
				return _nullTransformer.GetBooleanNullValue();
		}


		/// <summary>
		/// Получает значение указанного столбца в качестве логического значения.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public bool GetBoolean(string name)
		{
			return GetBoolean(GetOrdinal(name));
		}


		/// <summary>
		/// Получает 8-разрядное целое число без знака для указанного столбца.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public byte GetByte(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetByte(i);
			else
				return _nullTransformer.GetByteNullValue();
		}

		
		/// <summary>
		/// Получает 8-разрядное целое число без знака для указанного столбца.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public byte GetByte(string name)
		{
			return GetByte(GetOrdinal(name));
		}


		/// <summary>
		/// Считывает поток байтов из смещения указанного столбца в буфер в виде массива, начиная с указанного смещения буфера.
		/// </summary>
		/// <param name="i"></param>
		/// <param name="fieldOffset"></param>
		/// <param name="buffer"></param>
		/// <param name="bufferoffset"></param>
		/// <param name="length"></param>
		/// <returns></returns>
		public long GetBytes(int i, long fieldOffset, byte[] buffer, int bufferoffset, int length)
		{
			return _dataReader.GetBytes(i, fieldOffset, buffer, bufferoffset, length);
		}


		/// <summary>
		/// Получает значение символа для указанного столбца.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public char GetChar(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetChar(i);
			else
				return _nullTransformer.GetCharNullValue();
		}


		/// <summary>
		/// Получает значение символа для указанного столбца.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public char GetChar(string name)
		{
			return GetChar(GetOrdinal(name));
		}


		/// <summary>
		/// Считывает поток знаков из смещения указанного столбца в буфер в виде массива, начиная с указанного смещения буфера.
		/// </summary>
		/// <param name="i"></param>
		/// <param name="fieldoffset"></param>
		/// <param name="buffer"></param>
		/// <param name="bufferoffset"></param>
		/// <param name="length"></param>
		/// <returns></returns>
		public long GetChars(int i, long fieldoffset, char[] buffer, int bufferoffset, int length)
		{
			return _dataReader.GetChars(i, fieldoffset, buffer, bufferoffset, length);
		}


		/// <summary>
		/// Получает IDataReader, который используется, если поле указывает на внешние структурированные данные.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public System.Data.IDataReader GetData(int i)
		{
			return ((IDataRecord)_dataReader).GetData(i);
		}


		/// <summary>
		/// Получает сведения о типе данных для указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public string GetDataTypeName(int i)
		{
			return _dataReader.GetDataTypeName(i);
		}


		/// <summary>
		/// Получает значение даты и времени для указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public DateTime GetDateTime(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetDateTime(i);
			else
				return _nullTransformer.GetDateTimeNullValue();
		}


		/// <summary>
		/// Получает значение даты и времени для указанного поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public DateTime GetDateTime(string name)
		{
			return GetDateTime(GetOrdinal(name));
		}


		/// <summary>
		/// Получает числовое значение с фиксированным расположением для указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public decimal GetDecimal(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetDecimal(i);
			else
				return _nullTransformer.GetDecimalNullValue();
		}


		/// <summary>
		/// Получает числовое значение с фиксированным расположением для указанного поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public decimal GetDecimal(string name)
		{
			return GetDecimal(GetOrdinal(name));
		}


		/// <summary>
		/// Получает число с плавающей запятой удвоенной точности для указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public double GetDouble(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetDouble(i);
			else
				return _nullTransformer.GetDoubleNullValue();
		}


		/// <summary>
		/// Получает число с плавающей запятой удвоенной точности для указанного поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public double GetDouble(string name)

		{
			return GetDouble(GetOrdinal(name));
		}


		/// <summary>
		/// Получает сведения о Type, соответствующие типу объекта Object, возвращаемого из GetValue.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public System.Type GetFieldType(int i)
		{
			return _dataReader.GetFieldType(i);
		}


		/// <summary>
		/// Получает число с плавающей запятой обычной точности для указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public float GetFloat(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetFloat(i);
			else
				return _nullTransformer.GetSingleNullValue();
		}


		/// <summary>
		/// Получает число с плавающей запятой обычной точности для указанного поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public float GetFloat(string name)
		{
			return GetFloat(GetOrdinal(name));
		}

		
		/// <summary>
		/// Возвращает значение GUID указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public System.Guid GetGuid(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetGuid(i);
			else
				return _nullTransformer.GetGuidNullValue();
		}


		/// <summary>
		/// Возвращает значение GUID указанного поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public System.Guid GetGuid(string name)
		{
			return GetGuid(GetOrdinal(name));
		}


		/// <summary>
		/// Получает 16-разрядное целое число со знаком для указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public short GetInt16(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetInt16(i);
			else
				return _nullTransformer.GetInt16NullValue();
		}

		
		/// <summary>
		/// Получает 16-разрядное целое число со знаком для указанного поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public short GetInt16(string name)
		{
			return GetInt16(GetOrdinal(name));
		}


		/// <summary>
		/// Получает 32-разрядное целое число со знаком для указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public int GetInt32(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetInt32(i);
			else
				return _nullTransformer.GetInt32NullValue();
		}


		/// <summary>
		/// Получает 32-разрядное целое число со знаком для указанного поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public int GetInt32(string name)
		{
			return GetInt32(GetOrdinal(name));
		}


		/// <summary>
		/// Получает 64-разрядное целое число со знаком для указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public long GetInt64(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetInt64(i);
			else
				return _nullTransformer.GetInt64NullValue();
		}


		/// <summary>
		/// Получает 64-разрядное целое число со знаком для указанного поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public long GetInt64(string name)
		{
			return GetInt64(GetOrdinal(name));
		}


		/// <summary>
		/// Получает имя для поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public string GetName(int i)
		{
			return _dataReader.GetName(i);
		}


		/// <summary>
		/// Возвращает индекс поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public int GetOrdinal(string name)
		{
			if(_fieldNameLookup.Contains(name))
				return (int)_fieldNameLookup[name];
			else
			{
				int ordinal = _dataReader.GetOrdinal(name);
				_fieldNameLookup[name] = ordinal;
				return ordinal;
			}
		}


		/// <summary>
		/// Получает значение строки для указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public string GetString(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetString(i);
			else
				return _nullTransformer.GetStringNullValue();
		}

		
		/// <summary>
		/// Получает значение строки для указанного поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public string GetString(string name)
		{
			return GetString(GetOrdinal(name));
		}


		/// <summary>
		/// Возвращает значение указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public object GetValue(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetValue(i);
			else
				return _nullTransformer.GetNullValue(_dataReader.GetFieldType(i));
		}


		/// <summary>
		/// Возвращает значение указанного поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public object GetValue(string name)
		{
			return GetValue(GetOrdinal(name));
		}


		/// <summary>
		/// Получает коллекцию значений всех полей.
		/// </summary>
		/// <param name="values"></param>
		/// <returns></returns>
		public int GetValues(object[] values)
		{
			return _dataReader.GetValues(values);
		}


		/// <summary>
		/// Показывает, имеет ли указанное поле пустое значение.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public bool IsDBNull(int i)
		{
			return _dataReader.IsDBNull(i);
		}


		/// <summary>
		/// Получает число столбцов в текущей строке.
		/// </summary>
		public int FieldCount
		{
			get
			{
				return _dataReader.FieldCount;
			}
		}


		/// <summary>
		/// Получает указанный значение поля.
		/// </summary>
		public object this[string name]
		{
			get
			{
				return _dataReader[name];
			}
		}


		/// <summary>
		/// Получает указанный значение поля.
		/// </summary>
		public object this[int i]
		{
			get
			{
				return _dataReader[i];
			}
		}

		#endregion

		/// <summary>
		/// Получает 64-разрядное целое значение указанного поля.
		/// Метод может использоваться для чтений полей типа Int16, Int32, Int64.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public long GetInt(int i)
		{
			switch(_dataReader.GetDataTypeName(i))
			{
				case "smallint":
					return (long)GetInt16(i);
				case "int":
					return (long)GetInt32(i);
				case "bigint":
					return (long)GetInt64(i);
				default:
					throw new InvalidCastException(string.Format("Cast from {0} to (smallint, int or bigint) is not valid", _dataReader.GetDataTypeName(i)));
			}
		}


		/// <summary>
		/// Получает 64-разрядное целое значение указанного поля.
		/// Метод может использоваться для чтений полей типа Int16, Int32, Int64.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public long GetInt(string name)
		{
			return GetInt(GetOrdinal(name));
		}


		/// <summary>
		/// Получает числовое значение с фиксированным расположением для указанного поля.
		/// </summary>
		/// <param name="i"></param>
		/// <returns></returns>
		public decimal GetMoney(int i)
		{
			if(_nullTransformer == null || !_dataReader.IsDBNull(i))
				return _dataReader.GetDecimal(i);
			else
				return _nullTransformer.GetMoneyNullValue();
		}


		/// <summary>
		/// Получает числовое значение с фиксированным расположением для указанного поля.
		/// </summary>
		/// <param name="name"></param>
		/// <returns></returns>
		public decimal GetMoney(string name)
		{
			return GetMoney(GetOrdinal(name));
		}


		#region IDisposable implementation

		void IDisposable.Dispose()
		{
			((IDisposable)_dataReader).Dispose();
		}

		#endregion

		#region IEnumerable implementation

		System.Collections.IEnumerator IEnumerable.GetEnumerator()
		{
			return ((IEnumerable)_dataReader).GetEnumerator();
		}

		#endregion
	}
}