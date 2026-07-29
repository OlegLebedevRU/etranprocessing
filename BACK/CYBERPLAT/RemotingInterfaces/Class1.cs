namespace RemotingInterfaces
{
	public interface IDataExchange
	{
		bool	Encode(string[] par, int mode,out string url, out string msg);
		bool	Decode(string msg, out string resp);
		string	GetLastError();
	}
}

