using System;
using System.Collections;
using System.Xml;
using System.Runtime.Serialization;

namespace Estylesoft.Etran
{
	/// <summary>
	/// Ответ в формате платежной системы Рапида.
	/// </summary>
    [Serializable]
    public sealed class ResponseMessage
    {
        private readonly XmlDocument _responseXml = new XmlDocument();

        /// <summary>
        /// Конструктор.
        /// </summary>
        /// <param name="ResponseXml">Исходный XML-текст ответа платежной системы.</param>
		public ResponseMessage(string ResponseXml)
		{
            try
            {
                _responseXml.LoadXml(ResponseXml);
            }
            catch (XmlException ex)
            {
                throw new EtranException(this.GetType().FullName,"RapidaResponse(string)","Некорректный формат ответа платежной системы",ex);
            }
		}

        /// <summary>
        /// Результат выполнения запроса.
        /// </summary>
        public ResponseStatus Result 
        {
            get
            {
                XmlNode node = _responseXml.SelectSingleNode("Response/Result");

                if (node != null)
                {
                    return (ResponseStatus)Enum.Parse(typeof(ResponseStatus),node.InnerText,true);
                }
                else
                {
                    throw new EtranException(this.GetType().FullName,"get_Result","Не найден ожидаемый узел XML.",null);
                }
            }
        }

        /// <summary>
        /// Номер платежа в платежной системе.
        /// </summary>
        public string PaymNumb
        {
            get
            {
                XmlNode node = _responseXml.SelectSingleNode("Response/PaymNumb");

                if (node != null)
                {
                    return node.InnerText;
                }
                else
                {
                    return string.Empty;
                }
            }
        }

        /// <summary>
        /// Состояние платежа.
        /// </summary>
        public string PaymState
        {
            get
            {
                XmlNode node = _responseXml.SelectSingleNode("Response/PaymState");

                if (node != null)
                {
                    return node.InnerText;
                }
                else
                {
                    return string.Empty;
                }
            }
        }

        /// <summary>
        /// Уникальный номер платежа. При запросе на платеж, этот код должен соответствовать
        /// коду в запросе на проверку.
        /// </summary>
        public string PaymExtId
        {
            get
            {
                XmlNode node = _responseXml.SelectSingleNode("Response/PaymExtId");

                if (node != null)
                {
                    return node.InnerText;
                }
                else
                {
                    throw new EtranException(this.GetType().FullName,"get_PaymExtId","Не найден ожидаемый узел XML.",null);
                }
            }
        }

        /// <summary>
        /// Описание результата запроса.
        /// </summary>
        public string Description 
        { 
            get
            {
                XmlNode node = _responseXml.SelectSingleNode("Response/Description");

                if (node != null)
                {
                    return node.InnerText;
                }
                else
                {
                    throw new EtranException(this.GetType().FullName,"get_Description","Не найден ожидаемый узел XML.",null);
                }            
            }
        }

        /// <summary>
        /// Возвращает дополнительные параметры ответа. null - если параметр не найден.
        /// </summary>
        public string this[string Param]
        {
            get
            {
                XmlNode node;
                try
                {
                    node = _responseXml.SelectSingleNode(Param);
                }
                catch (XmlException ex)
                {
                    throw new EtranException(this.GetType().FullName,"this[string]","Некорректный формат имени дополнительного параметра",ex);
                }

                if (node != null)
                {
                    return node.InnerText;
                }
                else
                {
                    return null;
                }
            }
        }

        /// <summary>
        /// Возвращает исходный XML-текст ответа.
        /// </summary>
        /// <returns></returns>
        public override string ToString()
        {
            return _responseXml.InnerXml;
        }
	}
}
