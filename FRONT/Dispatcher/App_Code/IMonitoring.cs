using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.Serialization;
using System.ServiceModel;
using System.Text;




//Аналог нашего сервиса мониторинга, нужно использовать бизнес-логику нашего сервиса, а для совместимости любую залипуху сделать.
//Список проблем, которые нужно отправлять:
//1, "Купюроприемник недоступен" 
//2, "Принтер недоступен" 
//3, "Отсутствует бумага в принтере" 
//4, "Бумага в принтере близка к концу" 
//5, "Терминал недоступен" 
//6, "Картоприемник недоступен" 
//7, "Сканер штрих-кодов недоступен"

// NOTE: You can use the "Rename" command on the "Refactor" menu to change the interface name "IMonitoring" in both code and config file together.
[ServiceContract]
public interface IMonitoring
{
        /// <summary>
        /// Периодически вызываемая функция с признаком того, что постамат онлайн и описанием проблем в его работе
        /// </summary>
        /// <param name="postboxNumber">Номер постамата</param>
        /// <param name="problems">Текущие проблемы в работе постамата</param>
        [OperationContract]
        void PostboxOnline(string postboxNumber, List<SharedPostboxProblemInfo> problems//,
                                                                                        /*out List<SharedMailingInfo> overdueParcels*/ );


        /// <summary>
        /// Возвращает список просроченных посылок
        /// </summary>
        /// <param name="postboxNumber">Номер постамата</param>
        /// <param name="overdueParcels">Просроченные посылки</param>
        [OperationContract]
        void CheckOverdueParcels(string postboxNumber, out List<SharedMailingInfo> overdueParcels);


        /// <summary>
        /// Авторизация сотрудника ПР
        /// Требуется методам ParcelCodeScanned и ActScanned
        /// </summary>
        /// <param name="postboxNumber">Номер постамата</param>
        /// <param name="login">Логин</param>
        /// <param name="password">Пароль</param>
        /// <param name="uInfo">Информация о пользователе</param>
        /// <returns>Возвращает признак того, что пара известна системе</returns>
        [OperationContract]
        bool Authorize(string postboxNumber, string login, string password, out SharedUserInfo uInfo);


        /// <summary>
        /// Сотрудник ПР сосканировал код на посылке
        /// </summary>
        /// <param name="postboxNumber">Номер постамата</param>
        /// <param name="parcelNumber">Номер посылки</param>
        /// <param name="boxNumber">Выходной параметр: Номер ячейки, куда положить посылку</param>
        /// <param name="code">Выходной параметр: Сгенерированный код, с помощью которого адресат заберет посылку. Нужен для автономного режима</param>
        /// <param name="tariff">Выходной параметр: Тариф, который нужно взять с адресата при получении. Нужен для автономного режима</param>
        /// <param name="phoneNumber">Выходной параметр: Телефон на который пользователю приходят смс. Нужен для сдачи по умолчанию.</param>
        [OperationContract]
        void ParcelCodeScanned(string postboxNumber, string parcelNumber, out string boxNumber,
            out string code, out decimal tariff, out string phoneNumber);


        /// <summary>
        /// Сотрудник ПР положил посылку в ячейку
        /// </summary>
        /// <param name="postboxNumber">Номер постамата</param>
        /// <param name="operationId">ИД операции для защиты от срабатывания одинаковых запросов</param>
        /// <param name="boxNumber">Номер ячейки</param>
        /// <param name="parcelNumber">Номер посылки</param>
        [OperationContract]
        void ParcelToBox(string postboxNumber, Guid operationId, string boxNumber, string parcelNumber);


        /// <summary>
        /// Посылка успешно доставлена
        /// </summary>
        /// <param name="postboxNumber">Номер постамата</param>
        /// <param name="operationId">ИД операции для защиты от срабатывания одинаковых запросов</param>
        /// <param name="boxNumber">Номер ячейки</param>
        [OperationContract]
        void ParcelDelivered(string postboxNumber, Guid operationId, string boxNumber);


        /// <summary>
        /// Платеж
        /// </summary>
        /// <param name="postboxNumber">Номер постамата</param>
        /// <param name="operationId">ИД операции для защиты от срабатывания одинаковых запросов</param>
        /// <param name="boxNumber">Номер ячейки, из которой адресат забирает посылку</param>
        /// <param name="pInfo">Информация о платеже</param>
        /// <param name="res"></param>
        [OperationContract]
        void Payment(string postboxNumber, Guid operationId, string boxNumber, SharedPaymentInfo pInfo, out SharedRapidaResult res);


        /// <summary>
        /// Инкассация
        /// </summary>
        /// <param name="postboxNumber">Номер постамата</param>
        /// <param name="operationId">ИД операции для защиты от повторных срабатываний</param>
        /// <param name="cInfo">Информация об инкассации</param>
        [OperationContract]
        void Collection(string postboxNumber, Guid operationId, SharedCollectionInfo cInfo);


        /// <summary>
        /// Открыта смена постамата
        /// </summary>
        /// <param name="postboxNumber"></param>
        /// <param name="shiftNum"></param>
        /// <param name="frNumber"></param>
        /// <param name="frShiftNum"></param>
        /// <param name="userId"></param>
        /// <param name="dateOpened"></param>
        [OperationContract]
        void ShiftOpen(string postboxNumber, UInt64 shiftNum, string frNumber, int frShiftNum, string userId, DateTime dateOpened);

        /// <summary>
        /// Закрыта смена постамата
        /// </summary>
        /// <param name="postboxNumber"></param>
        /// <param name="shiftNum"></param>
        /// <param name="userId"></param>
        /// <param name="dateClosed"></param>
        [OperationContract]
        void ShiftClose(string postboxNumber, UInt64 shiftNum, string userId, DateTime dateClosed);


        /// <summary>
        /// Информация об инкассации
        /// </summary>
    }

public class SharedCollectionInfo
{
    /// <summary>
    /// Номер инкассации
    /// </summary>
    public UInt64 CollectionNum { get; set; }

    /// <summary>
    /// Номер документа об инкассации (печатается на ФР)
    /// </summary>
    public int CollectionDocNum { get; set; }

    /// <summary>
    /// Номер ФР
    /// </summary>
    public string FrNum { get; set; }

    /// <summary>
    /// Дата-время совершения инкассации
    /// </summary>
    public DateTime Timestamp { get; set; }

    /// <summary>
    /// Табельный номер пользователя совершившего инкассацию
    /// </summary>
    public string UserId { get; set; }

    /// <summary>
    /// Номер смены в которой произошла инкассация
    /// </summary>
    public int ShiftId { get; set; }

    /// <summary>
    /// Сумма изъятая купюрами (в копейках)
    /// </summary>
    public Int64 AmountBills { get; set; }

    /// <summary>
    /// Сумма изъятая монетами (в копейках)
    /// </summary>
    public int AmountCoins { get; set; }

    /// <summary>
    /// Сумма полученная безналичным способом (в копейках)
    /// </summary>
    public Int64 AmountCashless { get; set; }

    /// <summary>
    /// Начальный номер финансовой операции, попавшей в инкассацию (включительно)
    /// </summary>
    public UInt64 FinOpNumStart { get; set; }

    /// <summary>
    /// Конечный номер финансовой операции, попавшей в инкассацию (включительно)
    /// </summary>
    public UInt64 FinOpNumFin { get; set; }


    /// <summary>
    /// Конструктор по умолчанию
    /// </summary>
    public SharedCollectionInfo()
    {

    }

    /// <summary>
    /// Конструктор с параметрами
    /// </summary>
    /// <param name="num">Номер инкассации</param>
    /// <param name="docNum">Номер документа ФР об инкассации</param>
    /// <param name="frNum">Номер ФР</param>
    /// <param name="timestamp">Дата-время совершения инкассации</param>
    /// <param name="userId">Логин пользователя</param>
    public SharedCollectionInfo(UInt64 num, int docNum, string frNum, DateTime timestamp, string userId)
    {
        CollectionNum = num;
        CollectionDocNum = docNum;
        FrNum = frNum;
        Timestamp = timestamp;
        UserId = userId;
    }
}
public class SharedMailingInfo
{
    /// <summary>
    /// Номер отправления в системе Почты России
    /// </summary>
    public string Number { get; set; }

    /// <summary>
    /// Номер ячейки, в которой лежит посылка на момент передачи сообщения
    /// </summary>
    public string BoxNumber { get; set; }
}
/// <summary>
/// Описание проблемы в работе постамата
/// </summary>
public class SharedPostboxProblemInfo
{
    /// <summary>
    /// Номер вида проблемы
    /// </summary>
    public int ProblemTypeId { get; set; }

    /// <summary>
    /// Описание проблемы
    /// </summary>
    public string Description { get; set; }
}

public class SharedUserInfo
{
    /// <summary>
    /// Имя
    /// </summary>
    public string Name { get; set; }

    /// <summary>
    /// Отчество
    /// </summary>
    public string Patronymic { get; set; }

    /// <summary>
    /// Фамилия
    /// </summary>
    public string Surname { get; set; }

    /// <summary>
    /// Роль
    /// </summary>
    public int Role { get; set; }

    /// <summary>
    /// ИД
    /// </summary>
    public string UserId { get; set; }
}
public class SharedPaymentInfo
{
    /// <summary>
    /// Номер телефона, введенный получателем для получения сдачи
    /// Может быть пустая строка или null, если нет сдачи
    /// </summary>
    public string PhoneNumber { get; set; }

    /// <summary>
    /// Имя оператора указанного мобильного телефона для сдачи
    /// Может быть пустым, если платеж без сдачи
    /// </summary>
    public string MobileOperatorName { get; set; }

    /// <summary>
    /// Общая сумма операции
    /// </summary>
    public Int64 Amount { get; set; }

    /// <summary>
    /// Сумма сдачи, 0, если сдача не нужна
    /// </summary>
    public Int64 ChangeAmount { get; set; }

    /// <summary>
    /// Назначение платежа
    /// 1 - переадресация в постамат
    /// 2 - сдача на телефон
    /// </summary>
    public int PaymentPurpose { get; set; }

    /// <summary>
    /// Тип платежа, 1 - наличные, 2 - безналичные
    /// </summary>
    public int PaymentType { get; set; }

    /// <summary>
    /// Дата-время совершения операции
    /// </summary>
    public DateTime Timestamp { get; set; }

    /// <summary>
    /// Номер операции
    /// </summary>
    public UInt64 FinOpNum { get; set; }

    /// <summary>
    /// Номер чека
    /// </summary>
    public int ChequeNum { get; set; }

    /// <summary>
    /// Номер смены, в которой был совершен платеж
    /// </summary>
    public int ShiftNum { get; set; }
}
public class SharedRapidaResult
{
    /// <summary>
    /// Признак наличия ошибки
    /// </summary>
    public bool IsError { get; set; }

    /// <summary>
    /// Уникальный номер платежа (денежного перевода) у Участника Системы.
    /// </summary>
    public string PaymExtId { get; set; }

    /// <summary>
    /// Номер платежа в системе Рапида
    /// </summary>
    public string PaymNumb { get; set; }

    /// <summary>
    /// Фактические дата и время платежа в Системе в формате YYYY-MM-DD hh:mm:ss
    /// </summary>
    public string PaymDate { get; set; }

    /// <summary>
    /// Клиентское описание результата обработки
    /// </summary>
    public string Description { get; set; }

    /// <summary>
    /// Цифровой код ответа по результатам обработки запроса
    /// </summary>
    public string ErrCode { get; set; }

    /// <summary>
    /// Номер платежа в системе получателя платежа
    /// </summary>
    public string BillRegId { get; set; }

    /// <summary>
    /// ИД точки продаж Рапиды
    /// </summary>
    public string TermId { get; set; }

    /// <summary>
    /// Имя оператора мобильной связи
    /// </summary>
    public string OperatorName { get; set; }
}


