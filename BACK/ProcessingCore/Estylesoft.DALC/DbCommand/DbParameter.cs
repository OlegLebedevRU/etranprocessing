using System;
using System.Data;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Предоставляет параметр для <see cref="DbCommand"/>.
	/// </summary>
	public class DbParameter : IDataParameter
	{
		/// <summary>
		/// Инициализирует новый экземпляр класса <see cref="DbParameter"/> 
		/// именем <b>parameterName</b> и значением <b>value</b>.
		/// </summary>
		/// <param name="parameterName">Имя параметра.</param>
		/// <param name="value">Значение параметра.</param>
		internal DbParameter(string parameterName, object value)
		{
			_parameterName = parameterName;
			_value = value;
		}


		#region Implementation of IDataParameter

		private object _value;
		/// <summary>
		/// Значение параметра.
		/// </summary>
		public object Value
		{
			get { return _value; }
			set { _value = value; }
		}


		private string _parameterName;
		/// <summary>
		/// Имя параметра.
		/// </summary>
		public string ParameterName
		{
			get { return _parameterName; }
			set { _parameterName = value; }
		}


		#region Stubs
		
		System.Data.ParameterDirection IDataParameter.Direction
		{
			get { return new System.Data.ParameterDirection(); }
			set { }
		}

		System.Data.DbType IDataParameter.DbType
		{
			get {  return new System.Data.DbType(); }
			set { }
		}

		bool IDataParameter.IsNullable
		{
			get { return true; }
		}

		System.Data.DataRowVersion IDataParameter.SourceVersion
		{
			get { return new System.Data.DataRowVersion(); }
			set { }
		}

		string IDataParameter.SourceColumn
		{
			get { return null; }
			set { }
		}

		#endregion

		#endregion
	}
}
