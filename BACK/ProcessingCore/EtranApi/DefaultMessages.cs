using System;
using System.Xml;
using System.Text;

namespace Estylesoft.Etran
{
	/// <summary>
	/// Класс, создающий экземпляры стандартных ответов системы.
	/// </summary>
	public class DefaultMessages
	{
		private DefaultMessages() {}

        private const string PAYMENT_ACCEPTED_MESSAGE = "Платеж принят на обработку.";
        private const string PAYMENT_CAUSED_EXCEPTION_MESSAGE = "Платеж вызвал ошибку и не может быть проведен.";

//        private const string PAYMENT_REJECTED_MESSAGE = "Платеж не принят на обработку, сервер занят.";
//        private const string PAYCODE_UNSUPORTED_MESSAGE = "Платежный модуль не поддерживает данный код назначения";

        /// <summary>
        /// Генерирует XML ответа с заданными параметрами.
        /// </summary>
        /// <param name="Request">Исходный запрос.</param>
        /// <param name="Result">Результат операции.</param>
        /// <param name="Description">Описание результата.</param>
        /// <returns>XML-строку ответа платежной системы.</returns>
        static private string FormResponseXml(RequestMessage Request,ResponseStatus Result,string Description)
        {
            XmlDocument doc = new XmlDocument();

            doc.AppendChild(doc.CreateXmlDeclaration( "1.0","windows-1251",""));
            //doc.AppendChild(doc.CreateXmlDeclaration("1.0", "utf-8", ""));

            XmlNode response = doc.CreateElement("Response");

            XmlNode node;

            node = doc.CreateElement("Result");

            node.InnerText = Result.ToString();
            response.AppendChild(node);

            node = doc.CreateElement("PaymNumb");
            node.InnerText = (Request.PaymentID>0)?Request.PaymentID.ToString():string.Empty;
            response.AppendChild(node);

            node = doc.CreateElement("PaymState");
            node.InnerText = (Request.PaymState > 0 ) ? Request.PaymState.ToString() : string.Empty;
            response.AppendChild(node);

            node = doc.CreateElement("PaymExtId");
            node.InnerText = Request.PaymExtId;
            response.AppendChild(node);

            node = doc.CreateElement("Description");
            node.InnerText = Description;
            response.AppendChild(node);

            doc.AppendChild(response);

            return doc.InnerXml;
        }

        /// <summary>
        /// Возвращает стандартный ответ системы, при успешной отправке платежного сообщения
        /// на асинхронную обработку.
        /// </summary>
        /// <param name="Request">Запрос, на который генерируется ответ.</param>
        /// <returns></returns>
        static public ResponseMessage GetDefaultOKResponse(RequestMessage Request)
        {
            return new ResponseMessage(FormResponseXml(Request,ResponseStatus.OK,PAYMENT_ACCEPTED_MESSAGE));
        }

        static public ResponseMessage GetDefaultOKResponse(RequestMessage Request, string descr)
        {
            return new ResponseMessage(FormResponseXml(Request, ResponseStatus.OK, descr));
        }


        /// <summary>
        /// Возвращает ответ системы, при отбросе сообщения, в следствии переполненния пула потоков.
        /// </summary>
        /// <param name="Request">Запрос, на который генерируется ответ.</param>
        /// <returns></returns>
        //static public ResponseMessage GetRequestRejectedResponse(RequestMessage Request)
        //{
        //    return new ResponseMessage(FormResponseXml(Request,ResponseStatus.Error,PAYMENT_REJECTED_MESSAGE));
        //}
    
        /// <summary>
        /// Возвращает ответ системы, при отсутствии возможности обработать платеж из за внутренней ошибки.
        /// </summary>
        /// <param name="Request">Запрос, на который генерируется ответ.</param>
        /// <returns></returns>
        static public ResponseMessage GetDefaultErrorResponse(RequestMessage Request)
        {
            return new ResponseMessage(FormResponseXml(Request,ResponseStatus.Error,PAYMENT_CAUSED_EXCEPTION_MESSAGE));
        }

        /// <summary>
        /// Возвращает ответ системы, при отсутствии возможности обработать платеж из за внутренней ошибки.
        /// </summary>
        /// <param name="Request">Запрос, на который генерируется ответ.</param>
        /// <param name="Message">Текст исключения.</param>
        /// <returns></returns>
        static public ResponseMessage GetDefaultErrorResponse(RequestMessage Request,string Message)
        {
            return new ResponseMessage(FormResponseXml(Request,ResponseStatus.Error,PAYMENT_CAUSED_EXCEPTION_MESSAGE + " : " + Message));
        }

        /// <summary>
        /// Возвращает ответ системы, в случае если код назначения не поддерживается.
        /// </summary>
        /// <param name="Request">Запрос, на который генерируется ответ.</param>
        /// <returns></returns>
        //static public ResponseMessage GetPayCodeUnsupportedResponse(RequestMessage Request)
        //{
        //    return new ResponseMessage(FormResponseXml(Request, ResponseStatus.Error, PAYCODE_UNSUPORTED_MESSAGE));
        //}
    }
}
