unit libipriv;

interface
{*
   CopyRight (C) 1998-2005 CyberPlat.Com. All Rights Reserved.
   e-mail: support@cyberplat.com
*}


{$ifndef __LIBIPRIV_H}
 {$define __LIBIPRIV_H}
{$endif}

// Коды ошибок
const
CRYPT_ERR_BAD_ARGS=			-1;	// Ошибка в аргументах
CRYPT_ERR_OUT_OF_MEMORY=		-2;	// Ошибка выделения памяти
CRYPT_ERR_INVALIDFORMAT=		-3;	// Неверный формат документа
CRYPT_ERR_NO_DATA_FOUND=		-4;	// Документ прочитан не до конца
CRYPT_ERR_INVALID_PACKET_FORMAT=	-5;	// Ошибка во внутренней структуре документа
CRYPT_ERR_UNKNOWN_ALG=			-6;	// Неизвестный алгоритм шифрования
CRYPT_ERR_INVALID_KEYLEN=		-7;	// Длина ключа не соответствует длине подписи
CRYPT_ERR_INVALID_PASSWD=		-8;	// Неверная кодовая фраза закрытого ключа
CRYPT_ERR_DOCTYPE=			-9;	// Неверный тип документа
CRYPT_ERR_RADIX_DECODE=			-10;	// Ошибка ASCII кодирования документа
CRYPT_ERR_RADIX_ENCODE=			-11;	// Ошибка ASCII декодирования документа
CRYPT_ERR_INVALID_ENG=			-12;	// Неизвестный тип криптосредства
CRYPT_ERR_ENG_NOT_READY=		-13;	// Криптосредство не готово
CRYPT_ERR_NOT_SUPPORT=			-14;	// Вызов не поддерживается криптосредством
CRYPT_ERR_FILE_NOT_FOUND=		-15;	// Файл не найден
CRYPT_ERR_CANT_READ_FILE=		-16;	// Ошибка чтения файла
CRYPT_ERR_INVALID_KEY=			-17;	// Ключ не может быть использован
CRYPT_ERR_SEC_ENC=			-18;	// Ошибка формирования подписи
CRYPT_ERR_PUB_KEY_NOT_FOUND=		-19;	// Открытый ключ с таким серийным номером отсутствует
CRYPT_ERR_VERIFY=			-20;	// Подпись не соответствует содержимому документа
CRYPT_ERR_CREATE_FILE=			-21;	// Ошибка создания файла
CRYPT_ERR_CANT_WRITE_FILE=		-22;	// Ошибка записи в файл
CRYPT_ERR_INVALID_KEYCARD=		-23;	// Неверный формат карточки ключа
CRYPT_ERR_GENKEY=			-24;	// Ошибка генерации ключей
CRYPT_ERR_PUB_ENC=			-25;	// Ошибка шифрования
CRYPT_ERR_SEC_DEC=			-26;	// Ошибка дешифрации

 // Типы криптосредств
IPRIV_ENGINE_RSAREF=			0;	// Библиотека RSAREF
IPRIV_ENGINE_OPENSSL=			1;	// Библиотека OpenSSL
IPRIV_ENGINE_PKCS11=			2;	// Интерфейс PKCS11 (частный случай eToken)
IPRIV_ENGINE_WINCRYPT=			3;	// Интерфейс Microsoft Windows CryptoAPI

IPRIV_ENGINE_DEFAULT=			IPRIV_ENGINE_RSAREF;
IPRIV_DEFAULT_ENGINE=			IPRIV_ENGINE_DEFAULT;

// Максимальное количество поддерживаемых криптосредств
IPRIV_MAX_ENG_NUM=			4;

// Типы запросов к криптосредствам (используется при вызове Crypt_Ctrl)
IPRIV_ENGCMD_IS_READY=			0;	// in: none, retval: 1-ready, 0 - not ready
IPRIV_ENGCMD_GET_ERROR=			1;	// in: none, retval: errcode
IPRIV_ENGCMD_SET_PIN=			2;	// in: const char* null-terminated pin code, retval: 0-success
IPRIV_ENGCMD_SET_PKCS11_LIB=		3;	// in: static const char* null-terminated path to library, retval: 0-success
IPRIV_ENGCMD_GET_PKCS11_SLOTS_NUM=	4;	// in: none, retval - slots num or 0
IPRIV_ENGCMD_GET_PKCS11_SLOT_NAME=	5;	// in: int slot index, char* dst, int ndst, retval - string length
IPRIV_ENGCMD_SET_PKCS11_SLOT=		6;	// in: int slot index (from 0), retval - 0-success
IPRIV_ENGCMD_ENUM_PKCS11_KEYS=		7;	// in: IPRIV_KEY* array, int array max size, retval - keys num
IPRIV_ENGCMD_ENUM_PKCS11_PUBKEYS=	8;	// in: IPRIV_KEY* array, int array max size, retval - keys num

// Типы ключей
IPRIV_KEY_TYPE_RSA_SECRET=		1;
IPRIV_KEY_TYPE_RSA_PUBLIC=		2;

// Максимальная длина кода покупателя
MAX_USERID_LENGTH=			20;


{$ifdef _WIN32)
  {$define	IPRIVAPI	__stdcall}
{$else}
  {$define	IPRIVAPI}
{$endif}

type
// Структура ключа
IPRIV_KEY = record
	 eng : smallint;				// Тип криптосредства
	 types : smallint;				// Тип ключа
	 keyserial : longword;				// Серийный номер ключа
	 userid: array[1..24] of Char;			// Код покупателя
	 key : pointer;					// Специфические для криптосредства данные
end ;
PIPRIV_KEY=^IPRIV_KEY;

PPChar =^PChar;
PInteger =^Integer;


// ************************
// Интерфейс библиотеки   *
// ************************


// Инициализация библиотеки.
// Должна выполняться только один раз при запуски приложения (в основном потоке).
// Возвращает: 0 - успех или код ошибки
function Crypt_Initialize: integer; stdcall external 'libipriv.dll';

// Произвольный запрос к криптопровайдеру.
// Необходимо для обращения к нестандартным функциям криптопровайдера.
// Например, установка пин-кода для доступа к электронному ключу eToken.
// eng: входной, тип криптопровайдера
// cmd: входной, тип запроса
// Возвращает: зависит от типа запроса

function Crypt_Ctrl_Null(eng:integer;cmd:integer):integer; stdcall external 'libipriv.dll';
function Crypt_Ctrl_String(eng:integer;cmd:integer;const arg:PChar):integer; stdcall external 'libipriv.dll';
function Crypt_Ctrl_Int(eng:integer;cmd:integer;arg:integer):integer; stdcall external 'libipriv.dll';
function Crypt_Ctrl_Ptr(eng:integer;cmd:integer;arg:pointer):integer; stdcall external 'libipriv.dll';


// Формирование карточки ключа в памяти (Crypt_GenKeyCard) или в файле (Crypt_GenKeyCardToFile).
// dst: выходной, буфер для приема тела карточки ключа
// ndst: входной, максимальная длина приемного буфера
// path: входной, путь к файлу для карточки ключа
// userid: входной, код покупателя
// keyserial: входной, серийный номер ключа
// Возвращает: длина тела карточки или код ошибки

function Crypt_GenKeyCard(dst:PChar;ndst:integer;const userid:PChar;keyserial:longWord):integer; stdcall external 'libipriv.dll';
function Crypt_GenKeyCardToFile(const path:PChar;const userid:PChar;keyserial:LongWord):integer; stdcall external 'libipriv.dll';

// Чтение карточки ключа
// path: входной, путь к файлу для карточки ключа
// keyserial: выходной, серийный номер ключа
// userid: выходной, код покупателя
// Возвращает: 0 - успех или код ошибки

function Crypt_ReadKeyCardFromFile(const path:PChar; keyserial:longword;userid:PChar):integer;  stdcall external 'libipriv.dll';

// Генерация пары ключей (закрытый/открытый) на основе карточки ключа.
// eng: входной, тип криптопровайдера
// src: входной, буфер с телом карточки ключа
// nsrc: входной, длина буфера, -1 - считается сама (должен быть нуль-терминатор)
// keycardpath: входной, путь к файлу с карточкой ключа
// sec: выходной, закрытый ключ
// pub: выходной, открытый ключ
// bits: входной, длина ключа в битах
// Возвращает: 0 - успех или код ошибки

function Crypt_GenKey(eng:integer;const src:PChar;nsrc:integer;sec:PIPRIV_KEY;pub:PIPRIV_KEY; bits:integer):integer; stdcall external 'libipriv.dll';
function Crypt_GenKeyFromFile(eng:integer;const keycardpath:Pchar;sec:PIPRIV_KEY;pub:PIPRIV_KEY; bits: integer):integer; stdcall external 'libipriv.dll';
function Crypt_GenKey2(eng:integer;keyserial:longword;const userid:PChar;sec:PIPRIV_KEY;pub:PIPRIV_KEY;bits:integer):integer; stdcall external 'libipriv.dll';

// Загрузка закрытого ключа из буфера в памяти (Crypt_OpenSecretKey), из файла (Crypt_OpenSecretKeyFromFile)
// или из внутреннего хранилища криптопровайдера (Crypt_OpenSecretKeyFromStore).
// eng: входной, тип криптопровайдера
// src: входной, буфер с телом закрытого ключа
// nsrc: входной, длина буфера, -1 - считается сама (должен быть нуль-терминатор)
// path: входной, путь к файлу с закрытым ключом
// passwd: входной, кодовая фраза для расшифровки закрытого ключа
// keyserial: входной, серийный номер закрытого ключа
// key: выходной, закрытый ключ
// Возвращает: 0 - успех или код ошибки

function Crypt_OpenSecretKey(eng:integer;const src:PChar;nsrc:integer;
                             const passwd:PChar; key:PIPRIV_KEY):integer;stdcall external 'libipriv.dll';
function Crypt_OpenSecretKeyFromFile(eng:integer;const path:PChar;
                                     const passwd:PChar; key:PIPRIV_KEY): integer; stdcall external 'libipriv.dll';
function Crypt_OpenSecretKeyFromStore(eng:longint;keyserial:longWord; key:PIPRIV_KEY):integer;stdcall external 'libipriv.dll';

// Загрузка открытого ключа из буфера в памяти (Crypt_OpenPublicKey), из файла (Crypt_OpenPublicKeyFromFile)
// или из внутреннего хранилища криптопровайдера (Crypt_OpenPublicKeyFromStore).
// eng: входной, тип криптопровайдера
// src: входной, буфер с телом открытого ключа
// nsrc: входной, длина буфера, -1 - считается сама (должен быть нуль-терминатор)
// path: входной, путь к файлу с открытыми ключами
// keyserial: входной, серийный номер открытого ключа
// key: выходной, открытый ключ
// cakey: входной, может быть 0, открытый ключ для проверки подписи ключа
// Возвращает: 0 - успех или код ошибки

function Crypt_OpenPublicKey(eng: longint;const src: PChar;nsrc: longint;
                             keyserial: longword; key,cakey:PIPRIV_KEY): integer;stdcall external 'libipriv.dll';
function Crypt_OpenPublicKeyFromFile(eng:longint;const path:Pchar;
                             keyserial: longword;key,cakey:PIPRIV_KEY): integer; stdcall external 'libipriv.dll';
function Crypt_OpenPublicKeyFromStore(eng:longint; keyserial:longWord; key:PIPRIV_KEY): integer;stdcall external 'libipriv.dll';

// Экспорт закрытого ключа (может не поддерживаться криптопровайдером).
// dst: выходной, буфер для приема закрытого ключа
// ndst: входной, максимальная длина приемного буфера
// path: входной, путь к файлу для закрытого ключа
// passwd: входной, кодовая фраза для шифрования закрытого ключа
// key: входной, закрытый ключ
// Возвращает: длина тела ключа или код ошибки

function Crypt_ExportSecretKey(dst:PChar;ndst:integer;const passwd:PChar; key:PIPRIV_KEY): integer;stdcall external 'libipriv.dll';
function Crypt_ExportSecretKeyToFile(const path:PChar;const passwd:PChar; key:PIPRIV_KEY): integer;stdcall external 'libipriv.dll';

// Экспорт открытого ключа.
// dst: выходной, буфер для приема открытого ключа
// ndst: входной, максимальная длина приемного буфера
// path: входной, путь к файлу с открытыми ключами
// key: входной, открытый ключ
// cakey: входной, может быть 0, закрытый ключ для формирования подписи открытого ключа
// Возвращает: длина тела ключа или код ошибки

function Crypt_ExportPublicKey( dst: PChar;ndst:integer;key:PIPRIV_KEY;cakey:PIPRIV_KEY): integer;stdcall external 'libipriv.dll';
function Crypt_ExportPublicKeyToFile(const path:PChar;key:PIPRIV_KEY; cakey:PIPRIV_KEY): integer;stdcall external 'libipriv.dll';

// Импорт закрытого ключа во внутреннее хранилище криптопровайдера (может не поддерживаться).
// Например, импорт закрытого ключа покупателя в электронный ключ eToken.
// eng: входной, тип криптопровайдера
// src: входной, буфер с телом закрытого ключа
// nsrc: входной, длина буфера, -1 - считается сама (должен быть нуль-терминатор)
// path: входной, путь к файлу с закрытым ключом
// passwd: входной, кодовая фраза для расшифровки закрытого ключа
// Возвращает: 0 - успех или код ошибки

function Crypt_ImportSecretKey(eng:integer;const src:PChar;nsrc:integer;const passwd:PChar): integer;stdcall external 'libipriv.dll';
function Crypt_ImportSecretKeyFromFile(eng:integer;const path:PChar;const passwd:PChar): integer;stdcall external 'libipriv.dll';

// Импорт открытого ключа во внутреннее хранилище криптопровайдера (может не поддерживаться).
// Например, импорт открытого ключа банка в электронный ключ eToken.
// eng: входной, тип криптопровайдера
// src: входной, буфер с телом открытого ключа
// nsrc: входной, длина буфера, -1 - считается сама (должен быть нуль-терминатор)
// path: входной, путь к файлу с открытыми ключами
// keyserial: входной, серийный номер открытого ключа
// cakey: входной, может быть 0, открытый ключ для проверки подписи ключа
// Возвращает: 0 - успех или код ошибки

function Crypt_ImportPublicKey(eng:integer; const src:PChar;nsrc:integer; keyserial:longword; cakey:PIPRIV_KEY): integer;stdcall external 'libipriv.dll';
function Crypt_ImportPublicKeyFromFile(eng:integer;const path:PChar;keyserial:longword;cakey:PIPRIV_KEY): integer;stdcall external 'libipriv.dll';

// Формирование подписи сообщения.
// src: входной, буфер с телом сообщения
// nsrc: длина сообщения, -1 - считается сама (должен быть нуль-терминатор)
// dst: выходной, буфер для приема тела подписанного сообщения
// ndst: входной, максимальная длина приемного буфера
// key: входной, закрытый ключ
// Возвращает: длина тела сообщения или код ошибки

function Crypt_Sign(const src:PChar;nsrc:integer;dst:PChar;ndst:integer; key:PIPRIV_KEY):integer;stdcall external 'libipriv.dll';

// Проверка подписи сообщения.
// src: входной, буфер с телом сообщения
// nsrc: длина сообщения, -1 - считается сама (должен быть нуль-терминатор)
// pdst: выходной, может быть 0, адрес указателя, в который помещается адрес оригинального сообщения (до подписи)
// pndst: выходной, может быть 0, адрес переменной, в которую помещается длина оригинального сообщения (до подписи)
// key: входной, открытый ключ
// Возвращает: 0 - успех или код ошибки

function Crypt_Verify(const src:PChar;nsrc:integer;const pdst:PPChar;
                      pndst:PInteger; key:PIPRIV_KEY):integer;stdcall external 'libipriv.dll';

// Шифрование открытым ключом. Длина сообщения не должна превышать длину ключа.
// src: входной, буфер с телом сообщения
// nsrc: длина сообщения, -1 - считается сама (должен быть нуль-терминатор)
// dst: выходной, буфер для приема зашифрованного сообщения
// ndst: входной, максимальная длина приемного буфера
// Возвращает: длина зашифрованного сообщения или код ошибки

function Crypt_Encrypt(const src:PChar;nsrc:integer;dst:PChar;ndst:integer;
			key:PIPRIV_KEY):integer;stdcall external 'libipriv.dll'; 

// Дешифрование закрытым ключом.
// src: входной, буфер с зашифрованным сообщением
// nsrc: длина зашифрованного сообщения, -1 - считается сама (должен быть нуль-терминатор)
// dst: выходной, буфер для приема сообщения
// ndst: входной, максимальная длина приемного буфера
// Возвращает: длина сообщения или код ошибки
function Crypt_Decrypt(const src:PChar;nsrc:integer;dst:PChar;ndst:integer;
			key:PIPRIV_KEY):integer;stdcall external 'libipriv.dll'; 

// Закрытие ключа.
// key: входной, открытый или закрытый ключ
// Возвращает: 0 - успех или код ошибки

function Crypt_CloseKey(key:PIPRIV_KEY): integer;stdcall external 'libipriv.dll';

// Деинициализация библиотеки.
// Должна выполняться только один раз при завершении приложения (в основном потоке).
// Возвращает: 0 - успех или код ошибки

function Crypt_Done :integer;stdcall external 'libipriv.dll';

implementation

end.
