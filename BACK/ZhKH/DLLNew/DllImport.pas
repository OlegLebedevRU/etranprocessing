unit DllImport;

interface
uses Types_Kvc;
function InitLib(KVCBasePath: PChar; KVCSogPath: PChar): Integer; stdcall;
{
  Назначение:
    Инициализирует внутренние переменные библиотеки
  Параметры:
    KVCBasePath: Папка, где находится база приема платежей rsn3ггмм.pp
    KVCSogPath: Папка, где находятся файлы .sog
  Результат: см. описание ошибок в файле Types.pas
}

function SetRegion(Reg: Integer): Integer; stdcall;
{
  Назначение:
    Инициализирует дополнительные внутренние переменные библиотеки
  Параметры:
    Reg : Код региона (Рязань-1, Клепики-4)
  Результат: см. описание ошибок в файле Types.pas
}

function SetParameters(BankCode, OperCode: Integer): Integer; stdcall;
{
  Назначение:
    Инициализирует дополнительные внутренние переменные библиотеки
  Параметры:
    BankCode : Код банка (СБРФ-11,ПВТБ-6)
    OperCode : Код оператора
  Результат: см. описание ошибок в файле Types.pas
}

function GetAccount(Adr1, Adr2: Integer; var Info: PInfo): Integer; stdcall;
{
  Назначение:
    Поиск лицевого счета и заполнение спец.структуры
  Параметры:
    Adr1: Первый код лицевого счета в формате XXXYYZZ (улица,дом,корпус)
    Adr2: Второй код лицевого счета в формате XXXYZZ (квартира,комната,КР)
    Info: Указатель на спец.структуру
  Результат: см. описание ошибок в файле Types.pas
  Примечание:
    Info создается средствами библиотеки и должен быть освобожден
    вызовом функции FreeAccount
}

function FreeAccount(var Info: PInfo): Integer; stdcall;
{
  Назначение:
    Освобождение памяти, занятой спецструктурой
  Параметры:
    Info: Указатель на спец.структуру
  Результат: см. описание ошибок в файле Types.pas
}

function FillCharges(Info: PInfo; Mode: Integer): Integer; stdcall;
{
  Назначение:
    Заполнение сумм начислений
  Параметры:
    Info: Указатель на спец.структуру
    Mode: Режим получения сумм
      0 - нулевые суммы
      BY_DOLG - по долгу на начало месяца
      BY_NACH - по начислениям текущего месяца
      BY_NACH or BY_DOLG - по долгу с учетом начислений
  Результат: см. описание ошибок в файле Types.pas
}

function GetSharedData(Info: PInfo): Integer; stdcall;
{
  Назначение:
    Получение информации, отказ от изменений сделанных прямой модификацией
    содержимого памяти, находящейся по адресу Info.Shared
  Параметры:
    Info: Указатель на спец.структуру
  Результат: см. описание ошибок в файле Types.pas
  Примечание:
    После вызова функции доступ к информации можно получить через Info.Shared
}

function PostSharedData(Info: PInfo): Integer; stdcall;
{
  Назначение:
    Послать информацию в библиотеку и получить результат обработки
  Параметры:
    Info: Указатель на спец.структуру
  Результат: см. описание ошибок в файле Types.pas
  Примечание:
    После вызова функции доступ к информации можно получить через Info.Shared
}

function GetTotalPayment(Info: PInfo): Integer; stdcall;
{
  Назначение:
    Получение общей суммы для оплаты
  Параметры:
    Info: Указатель на спец.структуру
  Результат:
    Сумма к оплате в копейках
}

function WritePayment(Info: PInfo): Integer; stdcall;
{
  Назначение:
    Запись платежа в файл приема
  Параметры:
    Info: Указатель на спец.структуру
  Результат: см. описание ошибок в файле Types.pas
}
function SetConsumption(Info: PInfo; Vid: Integer; Rashod: Integer): Integer; stdcall;
{ !!!!Устаревшее!!!!
  Назначение:
    Установить расход по счетчику во виду услуги
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Rashod: Расход (электроэнергия в Квт-ч, вода в 0.1 куб.м, отопление в 0.001 Гкал )
  Результат: см. описание ошибок в файле Types.pas
}

function SetConsumptionNum(Info: PInfo; Vid: Integer; Number: Integer; Rashod: Integer): Integer; stdcall;
{
  Назначение:
    Установить расход по счетчику во виду услуги
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Number: Номер счетчика
    Rashod: Расход (электроэнергия в Квт-ч, вода в 0.1 куб.м, отопление в 0.001 Гкал )
  Результат: см. описание ошибок в файле Types.pas
}

function GetConsumption(Info: PInfo; Vid: Integer; var Rashod: Integer): Integer; stdcall;
{ !!!!Устаревшее!!!!
  Назначение:
    Получить расход по счетчику во виду услуги
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Rashod: Расход (электроэнергия в Квт-ч, вода в 0.1 куб.м )
  Результат: см. описание ошибок в файле Types.pas
}
function GetConsumptionNum(Info: PInfo; Vid: Integer; Number: Integer; var Rashod: Integer): Integer; stdcall;
{
  Назначение:
    Получить расход по счетчику во виду услуги
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Number: Номер счетчика
    Rashod: Расход (электроэнергия в Квт-ч, вода в 0.1 куб.м, отопление в 0.001 Гкал )
  Результат: см. описание ошибок в файле Types.pas
}

function SetCharge(Info: PInfo; Vid: Integer; Sum: Integer): Integer; stdcall;
{
  Назначение:
    Установить сумму оплаты для услуги по начислению
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Sum: Сумма в копейках
  Результат: см. описание ошибок в файле Types.pas
}

function GetCharge(Info: PInfo; Vid: Integer; var Sum: Integer): Integer; stdcall;
{
  Назначение:
    Получить сумму оплаты для услуги по начислению
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Sum: Сумма в копейках
  Результат: см. описание ошибок в файле Types.pas
}

function GetChargeCnt(Info: PInfo; Vid: Integer; var Sum: Integer): Integer; stdcall;
{ !!!!Устаревшее!!!!
  Назначение:
    Получить сумму оплаты для услуги по счетчикам
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Sum: Сумма в копейках
  Результат: см. описание ошибок в файле Types.pas
}

function GetChargeCntNum(Info: PInfo; Vid: Integer; Number: Integer; var Sum: Integer): Integer; stdcall;
{
  Назначение:
    Получить сумму оплаты для услуги по счетчикам
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Number: Номер счетчика
    Sum: Сумма в копейках
  Результат: см. описание ошибок в файле Types.pas
}

function GetValueCnt(Info: PInfo; Vid: Integer; var Value: PChar): Integer; stdcall;
{ !!!!Устаревшее!!!!
  Назначение:
    Получить значение оплаченного счетчика
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Value: значение оплаченного счетчика
  Результат: см. описание ошибок в файле Types.pas
  Примечание: После вызова Value - указатель на внутреннюю строку.
    Перед использованием ее необходимо скопировать.
}

function GetValueCntNum(Info: PInfo; Vid: Integer; Number: Integer; var Value: PChar): Integer; stdcall;
{
  Назначение:
    Получить значение оплаченного счетчика
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Number: Номер счетчика
    Value: значение оплаченного счетчика
  Результат: см. описание ошибок в файле Types.pas
  Примечание: После вызова Value - указатель на внутреннюю строку.
    Перед использованием ее необходимо скопировать.
}
function SetConsumptionBySum(Info: PInfo; Vid: Integer; Summa: Integer): Integer; stdcall;
{ !!!!Устаревшее!!!!
  Назначение:
    Установить расход по счетчику, исходя из суммы оплаты
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Summa: Сумма оплаты
  Результат: см. описание ошибок в файле Types.pas
  Примечание: При расчете расхода по холодной и горячей воде изменяется расход
    по соответствующему водоотведению. Если на соответсвующем водоотведении
    уже была какая-то сумма, то она учитывается при расчете расхода.
}
function SetConsumptionBySumNum(Info: PInfo; Vid: Integer; Number: Integer; Summa: Integer): Integer; stdcall;
{
  Назначение:
    Установить расход по счетчику, исходя из суммы оплаты
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Number: Номер счетчика
    Summa: Сумма оплаты
  Результат: см. описание ошибок в файле Types.pas
  Примечание: При расчете расхода по холодной и горячей воде изменяется расход
    по соответствующему водоотведению. Если на соответсвующем водоотведении
    уже была какая-то сумма, то она учитывается при расчете расхода.
}

function GetServiceName(Info: PInfo; Vid: Integer): PChar; stdcall;
{
  Назначение:
    Получить название услуги
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
  Результат: Указатель на название услуги
}

function GetCounterName(Info: PInfo; Vid: Integer; Number: Integer): PChar; stdcall;
{
  Назначение:
    Получить название счетчика
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Number: Номер счетчика
  Результат: Указатель на название услуги
}


function DistributeMoney(Info: PInfo; Summa: Integer): Integer; stdcall;
{
  Назначение:
    Распределить оплату по услугам
  Параметры:
    Info: Указатель на спец.структуру
    Summa: Реальная сумма оплаты
  Результат: см. описание ошибок в файле Types.pas
}

function GetParamCnt(Vid: Integer; var Koeff: Double; var UoM: PChar): Integer; stdcall;
{
  Назначение:
    Получить параметры услуги по счетчику
  Параметры:
    Vid: Вид услуги
    Koeff: Коэффициент;
    UoM: Единица измерения
  Результат: см. описание ошибок в файле Types.pas
  Примечание: После вызова Uof - указатель на внутреннюю строку.
    Перед использованием ее необходимо скопировать.
}
function SetChargeCnt(Info: PInfo; Vid, SubVid: Integer; Sum: Integer): Integer; stdcall;
{
  Назначение:
    Установить сумму оплаты для услуги по счетчикам
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    SubVid: Подвид
    Sum: Сумма в копейках
  Результат: см. описание ошибок в файле Types.pas
}

function SetChargeCntNum(Info: PInfo; Vid, SubVid, Number: Integer; Sum: Integer): Integer; stdcall;
{
  Назначение:
    Установить сумму оплаты для услуги по счетчикам
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    SubVid: Подвид
    Number: Номер счетчика
    Sum: Сумма в копейках
  Результат: см. описание ошибок в файле Types.pas
}

function SetMonth(var Info: PInfo; Month: Integer): Integer; stdcall;
{
  Назначение:
    Установить месяц, за который платят
  Параметры:
    Info: Указатель на спец.структуру
    Month: Месяц, за который платят
  Результат: см. описание ошибок в файле Types.pas
}

function SetID(Info:PInfo;ID:Integer):Integer; stdcall;
{
  Назначение:
    Установить идентификатор платежа
  Параметры:
    Info: Указатель на спец.структуру
    ID: Идентификатор платежа
  Результат: см. описание ошибок в файле Types.pas
}

function GetFileInfo(FileName:PChar; var CountPay, SumPay: Integer): Integer; stdcall;
{
  Назначение:
    Получить информацию по платежам за день
  Параметры:
    FileName: Путь до SZG-файла
    CountPay: количество платежей
    SumPay: сумма платежей в копейках
  Результат: см. описание ошибок в файле Types.pas
}
function SetExpectedConsumptionNum(Info: PInfo; Vid: Integer; Number: Integer; Rashod: Integer): Integer; stdcall;
{
  Назначение:
    Установить расход по счетчику по виду услуги в текущем месяце (без оплаты)
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Number: Номер счетчика
    Rashod: Расход (электроэнергия в Квт-ч, вода в 0.1 куб.м, отопление в 0.001 Гкал )
  Результат: см. описание ошибок в файле Types.pas
}

function SetFactValueNum(Info: PInfo; Vid: Integer; Number: Integer; FZS: Integer; SetRashod:Boolean): Integer; stdcall;
{
  Назначение:
    Установить фактическое показание счетчика
  Параметры:
    Info: Указатель на спец.структуру
    Vid: Вид услуги
    Number: Номер счетчика
    FZS: Фактическое значение счетчика (электроэнергия в Квт-ч, вода в 0.1 куб.м, отопление в 0.001 Гкал )
    SetRashod: Рассчитывать ли расход по показанию счетчика
  Результат: см. описание ошибок в файле Types.pas
}



implementation
const
  KVCLibName = 'kvc.dll';
function InitLib; external KVCLibName;
function GetAccount; external KVCLibName;
function FreeAccount; external KVCLibName;
function SetConsumption; external KVCLibName;
function GetConsumption; external KVCLibName;
function SetConsumptionNum; external KVCLibName;
function GetConsumptionNum; external KVCLibName;
function SetCharge; external KVCLibName;
function GetCharge; external KVCLibName;
function FillCharges; external KVCLibName;
function GetTotalPayment; external KVCLibName;
function WritePayment; external KVCLibName;
function GetSharedData; external KVCLibName;
function PostSharedData; external KVCLibName;
function GetChargeCnt; external KVCLibName;
function GetChargeCntNum; external KVCLibName;
function GetValueCnt; external KVCLibName;
function GetValueCntNum; external KVCLibName;
function SetConsumptionBySum; external KVCLibName;
function SetConsumptionBySumNum; external KVCLibName;
function SetParameters; external KVCLibName;
function GetServiceName; external KVCLibName;
function DistributeMoney; external KVCLibName;
function GetParamCnt; external KVCLibName;
function SetRegion; external KVCLibName;
function GetCounterName; external KVCLibName;
function SetChargeCntNum; external KVCLibName;
function SetChargeCnt; external KVCLibName;
function SetMonth; external KVCLibName;
function SetID; external KVCLibName;
function GetFileInfo; external KVCLibName;
function SetExpectedConsumptionNum; external KVCLibName;
function SetFactValueNum; external KVCLibName;


end.

