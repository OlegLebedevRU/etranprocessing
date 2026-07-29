using System;
using System.Data;
using System.Collections;

namespace Estylesoft.DALC
{
	/// <summary>
	/// SqlHelperParameterCache provides functions to leverage a static cache of procedure parameters, and the
	/// ability to discover parameters for stored procedures at run-time.
	/// </summary>
	public sealed class SqlHelperParameterCache
	{
		#region private methods, variables, and constructors

		//Since this class provides only static methods, make the default constructor private to prevent 
		//instances from being created with "new SqlHelperParameterCache()".
		private SqlHelperParameterCache() {}

		private static Hashtable paramCache = Hashtable.Synchronized(new Hashtable());

		/// <summary>
		/// resolve at run time the appropriate set of SqlParameters for a stored procedure
		/// </summary>
		/// <param name="connection">a valid connection for deriving parameters</param>
		/// <param name="spName">the name of the stored procedure</param>
		/// <param name="includeReturnValueParameter">whether or not to include their return value parameter</param>
		/// <returns></returns>
		private static IDataParameter[] DiscoverSpParameterSet(DbConnection connection, string spName, bool includeReturnValueParameter)
		{
			using(IDbCommand cmd = connection.DbProvider.CreateCommand())
			{
				using(cmd.Connection = connection.DbProvider.CreateConnection())
				{
					cmd.Connection.ConnectionString = connection.ConnectionString;
					cmd.CommandText = spName;
					cmd.CommandType = CommandType.StoredProcedure;

					cmd.Connection.Open();

					connection.DbProvider.DeriveParameters(cmd);

					cmd.Connection.Close();
				}

				if(!includeReturnValueParameter) 
					cmd.Parameters.RemoveAt(0);

				IDataParameter[] discoveredParameters = new IDataParameter[cmd.Parameters.Count];

				cmd.Parameters.CopyTo(discoveredParameters, 0);

				return discoveredParameters;
			}
		}

		//deep copy of cached SqlParameter array
		private static IDataParameter[] CloneParameters(IDataParameter[] originalParameters)
		{
			IDataParameter[] clonedParameters = new IDataParameter[originalParameters.Length];

			for (int i = 0, j = originalParameters.Length; i < j; i++)
			{
				clonedParameters[i] = (IDataParameter)((ICloneable)originalParameters[i]).Clone();
			}

			return clonedParameters;
		}

		#endregion private methods, variables, and constructors

		#region caching functions

		/// <summary>
		/// add parameter array to the cache
		/// </summary>
		/// <param name="connectionString">a valid connection string for a SqlConnection</param>
		/// <param name="commandText">the stored procedure name or T-SQL command</param>
		/// <param name="commandParameters">an array of SqlParamters to be cached</param>
		public static void CacheParameterSet(string connectionString, string commandText, params IDataParameter[] commandParameters)
		{
			string hashKey = connectionString + ":" + commandText;

			paramCache[hashKey] = commandParameters;
		}

		/// <summary>
		/// retrieve a parameter array from the cache
		/// </summary>
		/// <param name="connectionString">a valid connection string for a SqlConnection</param>
		/// <param name="commandText">the stored procedure name or T-SQL command</param>
		/// <returns>an array of SqlParamters</returns>
		public static IDataParameter[] GetCachedParameterSet(string connectionString, string commandText)
		{
			string hashKey = connectionString + ":" + commandText;

			IDataParameter[] cachedParameters = (IDataParameter[])paramCache[hashKey];
			
			if (cachedParameters == null)
			{			
				return null;
			}
			else
			{
				return CloneParameters(cachedParameters);
			}
		}

		#endregion caching functions

		#region Parameter Discovery Functions

		/// <summary>
		/// Retrieves the set of SqlParameters appropriate for the stored procedure
		/// </summary>
		/// <remarks>
		/// This method will query the database for this information, and then store it in a cache for future requests.
		/// </remarks>
		/// <param name="connection">a valid connection for deriving parameters</param>
		/// <param name="spName">the name of the stored procedure</param>
		/// <returns>an array of SqlParameters</returns>
		public static IDataParameter[] GetSpParameterSet(DbConnection connection, string spName)
		{
			return GetSpParameterSet(connection, spName, true);
		}

		/// <summary>
		/// Retrieves the set of SqlParameters appropriate for the stored procedure
		/// </summary>
		/// <remarks>
		/// This method will query the database for this information, and then store it in a cache for future requests.
		/// </remarks>
		/// <param name="connection">a valid connection for deriving parameters</param>
		/// <param name="spName">the name of the stored procedure</param>
		/// <param name="includeReturnValueParameter">a bool value indicating whether the return value parameter should be included in the results</param>
		/// <returns>an array of SqlParameters</returns>
		public static IDataParameter[] GetSpParameterSet(DbConnection connection, string spName, bool includeReturnValueParameter)
		{
			string hashKey = connection.ConnectionString + ":" + spName + (includeReturnValueParameter ? ":include ReturnValue Parameter":String.Empty);

			IDataParameter[] cachedParameters;
			
			cachedParameters = (IDataParameter[])paramCache[hashKey];

			if (cachedParameters == null)
			{			
				cachedParameters = (IDataParameter[])(paramCache[hashKey] = DiscoverSpParameterSet(connection, spName, includeReturnValueParameter));
			}
			
			return CloneParameters(cachedParameters);
		}

		#endregion Parameter Discovery Functions
	}
}
