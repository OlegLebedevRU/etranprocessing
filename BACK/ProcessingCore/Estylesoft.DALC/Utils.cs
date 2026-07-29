using System;
using System.Data;

namespace Estylesoft.DALC
{
	/// <summary>
	///  ласс, предоставл€ющий вспомогательный функции дл€ DALC.
	/// </summary>
	public class Utils
	{
		/// <summary>
		/// ¬озвращает DbNull, в случае, если значение входного параметра равно null.
		/// </summary>
		/// <param name="val"></param>
		/// <returns></returns>
		static public object ClrNullToDbNull(object val)
		{
			if(val is DateTime)
				return ((DateTime)val == DateTime.MinValue)?System.DBNull.Value:val;
			else
                return (val == null)?System.DBNull.Value:val;
		}

		/// <summary>
		/// ¬озвращает null, в случае, если значение входного параметра равно DbNull.
		/// </summary>
		/// <param name="val"></param>
		/// <returns></returns>
		static public object DbNullToClrNull(object val)
		{
			return (val == System.DBNull.Value)?null:val;
		}

		/// <summary>
		/// Ёкземпл€р <see cref="IDbNullDataTransformer"/>-а, используемый "по умолчанию".
		/// </summary>
		static public readonly IDbNullDataTransformer DefaultNullDataTransformer = new DbNullDataTransformer();

		/// <summary>
		/// "Ѕезопасное" чтение значени€ полей <see cref="DataRow"/>.
		/// ≈сли значение пол€ равно DbNull, то оно преобразуетс€ с 
		/// использованием nullDataTransformer-а.
		/// </summary>
		/// <param name="row"></param>
		/// <param name="columnIndex"></param>
		/// <param name="nullDataTransformer"></param>
		/// <returns></returns>
		static public object ReadDataRowField(DataRow row, int columnIndex, IDbNullDataTransformer nullDataTransformer)
		{
			object val = row[columnIndex];

			if(val != DBNull.Value && val != null)
			{
				return nullDataTransformer.GetNullValue(row.Table.Columns[columnIndex].DataType);
			}
			else
				return row[columnIndex];
		}


		/// <summary>
		/// "Ѕезопасное" чтение значени€ полей <see cref="DataRow"/>.
		/// ≈сли значение пол€ равно DbNull, то оно преобразуетс€ с 
		/// использованием DefaultNullDataTransformer-а.
		/// </summary>
		/// <param name="row"></param>
		/// <param name="columnIndex"></param>
		/// <returns></returns>
		static public object ReadDataRowField(DataRow row, int columnIndex)
		{
			return ReadDataRowField(row, columnIndex, DefaultNullDataTransformer);
		}
	

		/// <summary>
		/// "Ѕезопасное" чтение значени€ полей <see cref="DataRow"/>.
		/// ≈сли значение пол€ равно DbNull, то оно преобразуетс€ с 
		/// использованием nullDataTransformer-а.
		/// </summary>
		/// <param name="row"></param>
		/// <param name="columnName"></param>
		/// <param name="nullDataTransformer"></param>
		/// <returns></returns>
		static public object ReadDataRowField(DataRow row, string columnName, IDbNullDataTransformer nullDataTransformer)
		{
			object val = row[columnName];

			if(val != DBNull.Value && val != null)
			{
				return nullDataTransformer.GetNullValue(row.Table.Columns[columnName].DataType);
			}
			else
				return val;
		}


		/// <summary>
		/// "Ѕезопасное" чтение значени€ полей <see cref="DataRow"/>.
		/// ≈сли значение пол€ равно DbNull, то оно преобразуетс€ с 
		/// использованием DefaultNullDataTransformer-а.
		/// </summary>
		/// <param name="row"></param>
		/// <param name="columnName"></param>
		/// <returns></returns>
		static public object ReadDataRowField(DataRow row, string columnName)
		{
			return ReadDataRowField(row, columnName, DefaultNullDataTransformer);
		}
	
	}
}
