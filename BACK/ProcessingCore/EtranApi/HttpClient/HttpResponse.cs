using System;
using System.Net;
using System.Collections;

namespace Estylesoft.Etran.HttpClient
{
	/// <summary>
	/// Класс, инкапсулирующий HTTP-ответ.
	/// </summary>
	public sealed class HttpResponse
	{
        private readonly string _body;
        private readonly Hashtable _headers;
        private readonly HttpStatusCode _code;
        private readonly string _statusDescription;

        /// <summary>
        /// Конструктор.
        /// </summary>
        /// <param name="Body">Тело ответа.</param>
        /// <param name="Headers">Заголовки ответа.</param>
        /// <param name="Code">Код статуса ответа.</param>
        /// <param name="StatusDescription">Описание кода статуса ответа.</param>
		public HttpResponse(string Body,Hashtable Headers,HttpStatusCode Code,string StatusDescription)
		{
            _body = Body;
            _headers = Headers;
            _code = Code;
            _statusDescription = StatusDescription;
		}

        /// <summary>
        /// Тело ответа.
        /// </summary>
        public string Body
        {
            get { return _body; }
        }

        /// <summary>
        /// Заголовки ответа.
        /// </summary>
        public Hashtable Headers
        {
            get { return _headers; }
        }

        /// <summary>
        /// Код статуса ответа.
        /// </summary>
        public HttpStatusCode StatusCode
        {
            get { return _code; }
        }

        /// <summary>
        /// Описание кода статуса ответа.
        /// </summary>
        public string StatusDescription
        {
            get { return _statusDescription; }
        }

        /// <summary>
        /// true - если код статуса OK (200), false - в любом другом случае.
        /// </summary>
        public bool IsOK
        {
            get { return _code == HttpStatusCode.OK; }
        }
	}
}
