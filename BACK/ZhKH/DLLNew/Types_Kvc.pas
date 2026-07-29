unit Types_Kvc;

interface
const
  BY_DOLG = 1;
  BY_NACH = 2;

  KVC_OK = 0; // Успешная операция
  KVC_ANY_ERROR = -1; // Неизвестная ошибка
  KVC_ZERO_PAYMENT = -2; // Попытка записать нулевой платеж
  KVC_FILE_NOT_FOUND = -3; // Файл не найден
  KVC_BUFFER_TOO_SMALL = -4; // Буфер слишком мал для получения данных (внутренняя)
  KVC_NULL_POINTER = -5; // Попытка обращение по нулевому указателю
  KVC_ACCOUNT_NOT_FOUND = -6; // Лицевой счет не существует
  KVC_BAD_PARAMETER = -7; // Вызов функции с недопустимым параметром
  KVC_DAY_CLOSED = -8; // Файл .SOG закрыт, сегодня прием платежей закончен
  KVC_BAD_FILE = -9; // Файл испорчен, необходимо прекратить работу
  KVC_FILE_NOT_OPEN = -10; // Не удалось открыть файл
  KVC_ALREADY_EXISTS = -11; // Платеж уже был записан

type
  TVSut = packed record // Структура файла приема:
    Code: Word; // Код
    Value: Cardinal; // Значение
  end;
  PVSutArray = ^TVSutArray;
  TVSutArray = array[0..0] of TVSut;

  TCntData = record
    Tar: Integer;
    Rash: Integer;
    Sum: Integer;
  end;

  PCnt = ^TCnt;
  TCnt = record
    Vid: Integer;
    SubVid: Integer;
    Prec: Integer;
    Rash: Integer;
    Sum: Integer;
    Number, ExpCons, DisablePay, FZS: Integer;
    Normal: TCntData;
    Higher: TCntData;
    Value: PChar;
//    Number: Integer; {!!!ВНИМАНИЕ!!!}
  end;
  PCntArray = ^TCntArray;
  TCntArray = array[0..0] of TCnt;

  TCntsData=record
     Size:Integer;
     CntData:PCntArray;
  end;
  PCntsDataArray=^TCntsDataArray;
  TCntsDataArray=array [0..1000] of TCntsData;

  PPay=^TPay;
  TPay = record
    Vid: Integer;
    Sal: Integer;
    Nac: Integer;
    Sum: Integer;
  end;
  PPayArray = ^TPayArray;
  TPayArray = array[0..0] of TPay;

  PInfo = ^TInfo;
  TInfo = record // Специальная структура для хранения информации лицевого счета
    Adr1: Integer; // Первый код л/с
    Adr2: Integer; // Второй код л/с
    PostAddress: PChar; // Почтовый адрес
    SharedLen: Integer; // Количество записей в области обмена данными
    Shared: PVSutArray; // Указатель на область обмена данными
    PayLen: Integer; // Количество записей по адресу Pay
    Pay: PPayArray; // Указатель на область данных услуг по начислению
    CntLen: Integer; // Количество записей по адресу Cnt
    Cnt: PCntArray; // Указатель на область данных услуг по счетчику
    MonthPay:Integer; // Месяц за который платят (ггмм)
//    CntData:array [0..1] of PCntArray; // Данные по счетчикам
    CntData:PCntsDataArray; // Данные по счетчикам
    CntMaxLen:Integer;
    Internal1:Pointer;
    Internal2:Pointer;
    ID:Integer;
  end;
  // Допустимо модифицировать первые SharedLen записей по адресу Shared
  // Остальные поля являются служебными


  PInfoRod = ^TInfoRod;
  TInfoRod = record
    DSNumber: Integer; // Номер учреждения
    LSNumber: Integer; // Лицевой счет
    FIO: PChar; // ФИО ребенка
    MonthPay: Integer; // Месяц за который платят (ггмм)
    Sum: Integer; // сумма оплаты
    ID:Integer;
  end;

implementation

end.

