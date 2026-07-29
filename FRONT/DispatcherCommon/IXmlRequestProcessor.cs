namespace DispatcherCommon
{
    public interface IXmlRequestProcessor
    {
        /// <summary>
        /// обработка запроса, в случаи ошибки - exception
        /// </summary>
        /// <param name="service">имя сервиса</param>
        /// <param name="request">обьект для десериализации</param>
        /// <returns>сериализованный обьект</returns>
        string DoRequest(string service, string request);
    }
}
