using System;
using System.Collections;
using System.Web;
using System.Net;
using System.IO;
using System.Text;
using System.Security.Cryptography.X509Certificates;

namespace Estylesoft.Etran.HttpClient
{
	/// <summary>
	/// Базовый класс клиента HTTP. Реализует методы отправки и получения HTTP сообщений.
	/// Является базовым для модулей сопряжения с платежными системами, работающими по протоколу
	/// HTTP.
	/// </summary>
	public abstract class HttpClient
	{
        private const string POST_VERB = "POST";

        private string _targetUrl = string.Empty;
        private Hashtable _queryString = new Hashtable();
        private Hashtable _headers = new Hashtable();
        private X509CertificateCollection _certificates = new X509CertificateCollection();

		/// <summary>
		/// Конструктор.
		/// </summary>
		/// <param name="TargetUrl">URL целевого Host'а.</param>
        protected HttpClient(string TargetUrl)
		{
            _targetUrl = TargetUrl;
		}

        /// <summary>
        /// URL целевого Host'а.
        /// </summary>
        public string TargetUrl
        {
            get { return _targetUrl; }
            set { _targetUrl = value; }
        }

        /// <summary>
        /// Параметры строки запроса.
        /// </summary>
        protected Hashtable QueryString
        {
            get { return _queryString; }
        }

        /// <summary>
        /// Заголовки запроса.
        /// </summary>
        protected Hashtable Headers
        {
            get { return _headers; }
        }
        
        /// <summary>
        /// Клиентские сертификаты.
        /// </summary>
        protected X509CertificateCollection Certificates
        {
            get {  return _certificates; }
        }

        /// <summary>
        /// Получает полный URL, включая URL целевого Host'а и параметры строки запроса.
        /// </summary>
        /// <returns>Полный URL запроса.</returns>
        private string GetUrl()
        {
            StringBuilder url = new StringBuilder(TargetUrl + "?");

            foreach (DictionaryEntry parameter in _queryString)
            {
                url.Append(HttpUtility.UrlEncode(parameter.Key.ToString()) + "=" + HttpUtility.UrlEncode(parameter.Value.ToString()));
                url.Append('&');
            }

            if (url .Length != TargetUrl.Length)
            {
                url.Remove(url.Length - 1,1);
            }

            return url.ToString();
        }

        /// <summary>
        /// Производит запрос к HTTP-серверу и заносит параметры ответа в объект HttpResponse.
        /// </summary>
        /// <param name="Request">Сформированный HTTP-запрос.</param>
        /// <returns>Сформированный объект HttpResponse.</returns>
        virtual protected HttpResponse ReceiveResponse(HttpWebRequest Request)
        {
            HttpWebResponse response;
            string responseBodyString = string.Empty;
            Hashtable responseHeaders = new Hashtable();
            
            try
            {
                response = (HttpWebResponse)Request.GetResponse();
				
                Stream responseBody = response.GetResponseStream();
                StreamReader reader = new StreamReader(responseBody, Encoding.GetEncoding("windows-1251"));
                //StreamReader reader = new StreamReader(responseBody, Encoding.GetEncoding("utf-8"));

                responseBodyString = reader.ReadToEnd();

                foreach (string key in response.Headers.AllKeys)
                {
                    responseHeaders.Add(key,response.Headers[key]);
                }
            }
			catch (WebException ex)
			{
				if (null!=ex.Response)
				{
					response = (HttpWebResponse)ex.Response;
				} 
				else
				{
					throw;
				}
			}

            return new HttpResponse(responseBodyString,responseHeaders,response.StatusCode,response.StatusDescription);
        }

        /// <summary>
        /// Производит запрос к HTTP-серверу и возвращает ответ в виде объекта HttpResponse.
        /// </summary>
        /// <param name="Method">Метод запроса (POST или GET)</param>
        /// <param name="RequestBody">Тело запроса (только для запросов POST).</param>
        /// <param name="Timeout">Максимальное время ожидания ответа от целевого host'а в секундах.</param>
        /// <returns>Сформированный HTTP-ответ.</returns>
        virtual protected HttpResponse SendRequest(string Method,string RequestBody,int Timeout)
        {
            HttpWebRequest request = (HttpWebRequest)WebRequest.Create(GetUrl());

            foreach (DictionaryEntry header in _headers)
            {
                request.Headers.Add(header.Key.ToString(),header.Value.ToString());
            }

            request.Method = Method;
            request.Timeout = Timeout*1000;

            if (Method.ToUpper() == POST_VERB)
            {
                Stream requestBody = request.GetRequestStream();
                StreamWriter writer = new StreamWriter(requestBody);

                writer.Write(RequestBody);

                writer.Close();
                requestBody.Close();
            }

            request.Proxy=System.Net.WebProxy.GetDefaultProxy();
			request.Credentials = System.Net.CredentialCache.DefaultCredentials;
            
            foreach(X509Certificate certificate in _certificates)
            {
                request.ClientCertificates.Add(certificate);
            }

            return ReceiveResponse(request);
        }
		
	}
}
