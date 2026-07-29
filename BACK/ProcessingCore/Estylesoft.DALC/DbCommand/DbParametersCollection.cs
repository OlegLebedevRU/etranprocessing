using System;
using System.Data;

namespace Estylesoft.DALC
{
	/// <summary>
	/// Коллекция параметров команды <see cref="DbCommand"/>.
	/// </summary>
	public class DbParametersCollection : DbCollectionBase, IDataParameterCollection
	{
		#region Implementation of IDataParameterCollection

		/// <summary>
		/// Удаляет параметр из коллекции.
		/// </summary>
		/// <param name="parameterName">Имя параметра.</param>
		public void RemoveAt(string parameterName)
		{
			RemoveAt(IndexOf(parameterName));
		}

		/// <summary>
		/// Проверяет, содержит ли коллекция параметр с указаным именем.
		/// </summary>
		/// <param name="parameterName">Имя параметра.</param>
		/// <returns>True, если коллекция содержит параметр с указаным именем.</returns>
		public bool Contains(string parameterName)
		{
			return (-1 != IndexOf(parameterName));
		}

		/// <summary>
		/// Возвращает индекс параметра с указаным именем.
		/// </summary>
		/// <param name="parameterName">Имя параметра.</param>
		/// <returns>Индекс параметра.</returns>
		public int IndexOf(string parameterName)
		{
			for(int i=0;i<List.Count;i++)
			{
				if(this[0].ParameterName == parameterName)
					return i;
			}

			return -1;
		}

		object IDataParameterCollection.this[string parameterName]
		{
			get { return this[IndexOf(parameterName)]; }
			set { this[IndexOf(parameterName)] = (DbParameter)value; }
		}
		
		/// <summary>
		/// Возвращает параметр по его наименованию.
		/// </summary>
		public DbParameter this[string parameterName]
		{
			get { return this[IndexOf(parameterName)]; }
			set { this[IndexOf(parameterName)] = value; }
		}

		#endregion

		#region CollectionBase implementation
		
		/// <summary>
		/// Возвращает параметр по его индексу.
		/// </summary>
		public DbParameter this[int index]
		{
			get { return (DbParameter)List[index]; }
			set { List[index] = value; }
		}

		/// <summary>
		/// Добавляет параметр в коллекцию.
		/// </summary>
		/// <param name="value">Параметр команды.</param>
		/// <returns>Индекс параметра в коллекции.</returns>
		public int Add(DbParameter value)
		{
			return List.Add(value);
		}

		/// <summary>
		/// Возвращает индекс параметра в коллекции.
		/// </summary>
		/// <param name="value">Параметр.</param>
		/// <returns>Индекс параметра.</returns>
		public int IndexOf(DbParameter value)
		{
			return List.IndexOf(value);
		}

		/// <summary>
		/// Добавляет параметр в коллецию в указанную позицию.
		/// </summary>
		/// <param name="index">Позиция, в которую нужно поместить добавляемый параметр.</param>
		/// <param name="value">Параметр.</param>
		public void Insert(int index, DbParameter value)
		{
			List.Insert(index, value);
		}

		/// <summary>
		/// Удалить параметр из коллекции.
		/// </summary>
		/// <param name="value">Параметр.</param>
		public void Remove(DbParameter value)
		{
			List.Remove(value);
		}

		/// <summary>
		/// Проверяет, содержит ли коллекция указанный параметр.
		/// </summary>
		/// <param name="value">Параметр</param>
		/// <returns>True, если коллекция содержит указанный параметр.</returns>
		public bool Contains( DbParameter value )  
		{
			return List.Contains(value);
		}

		#endregion
	}
}
