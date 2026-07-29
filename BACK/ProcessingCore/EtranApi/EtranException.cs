using System;

namespace Estylesoft.Etran
{
	/// <summary>
	/// Класс специфических сообщений системы Etran.
	/// </summary>
	public class EtranException : Exception
	{
        private string _class;
        private string _method;

        public string Class
        {
            get { return _class; }
        }

        public string Method
        {
            get { return _method; }
        }

		public EtranException(string Class,string Method,string Message,Exception InnerException) : base(Message,InnerException)
        {
            _class = Class;
            _method = Method;
        }
	}
}
