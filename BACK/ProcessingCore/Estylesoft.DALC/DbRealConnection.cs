using System;
using System.Data;

namespace Estylesoft.DALC
{
	internal class DbRealConnection
	{
		public DbRealConnection(DbConnection connection)
		{
			_connection = connection.DbProvider.CreateConnection();
			_connection.ConnectionString = connection.ConnectionString;
		}

		public IDbConnection _connection;
		public IDbConnection Connection
		{
			get { return _connection; }
		}

		public IDbTransaction _transaction;
		public IDbTransaction Transaction
		{
			get { return _transaction; }
		}

		public void BeginTransaction(DbIsolationLevel isolationLevel)
		{
			_connection.Open();
			_transaction = _connection.BeginTransaction(DbIsolationLevelHelper.ToIsolationLevel(isolationLevel));
		}
	}
}
