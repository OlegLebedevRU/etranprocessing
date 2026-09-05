# Руководство по интеграции, параметризации и сборке легаси-терминала PlaterraTerminal

Данный документ фиксирует архитектурную и прикладную специфику работы с легаси платёжным терминалом (`PlaterraSoft\Terminal`), сформированную по результатам исследования и практической доработки сценариев оплаты по `API ОСМП`.

---

## 1. Специфика настройки ТСП с параметризацией в БД Terminal

В архитектуре терминала используется модель наследования через **прототипы**, позволяющая конфигурировать общие сценарии ввода, валидации и шагов оплаты один раз, а затем привязывать к ним сотни конечных услуг.

### 1.1. Модель прототипов (tb_Tsp)
- **Запись-прототип:**
  - `tb_Tsp.Id = 99019` (диапазон прототипов: `99xxx`).
  - `tb_Tsp.Code = 0` (признак прототипа, а не конечной услуги).
  - `tb_Tsp.PrototypeId = 0`.
  - `tb_Tsp.PayTemplateId = 99019` (ссылается на собственную цепочку экранных форм в `tb_PayTemplates`).
- **Конечная услуга (дочернее ТСП):**
  - `tb_Tsp.Id = 1000001` (диапазон услуг: `1000xxx`).
  - `tb_Tsp.Code = 1000301` (реальный код услуги в биллинге).
  - `tb_Tsp.PrototypeId = 99019` (наследует шаблон, шаги и валидаторы прототипа).
  - `tb_Tsp.PayTemplateId = 0` (шаблон берется из прототипа ядром `clsPaymentPagesManager`).
  - `tb_Tsp.Dogovor = '120'` (опционально: фиксированная цена услуги, если не задается динамически).

### 1.2. Цепочка экранных форм (tb_PayTemplates и tb_PayTemplateForms)
Платёжный сценарий задается упорядоченным набором шагов (`Order = 1..N`) с привязкой к экранным классам через `FormId` (`tb_Forms`):
1. `FormId = 10` (`pgInpCell`, `Order = 1`) — Окно ввода реквизита (номер счета, телефон, договор).
2. `FormId = 14` (`pgCheckParameters`, `Order = 2`) — Окно подтверждения параметров (показ ФИО, баланса, задолженности).
3. `FormId = 64` (`pgSelectPayType`, `Order = 3`) — Окно выбора способа оплаты (наличные, банковская карта, СБП).
4. `FormId = 12` (`pgGetMoney`, `Order = 4`) — Окно приема оплаты (купюроприемник или вызов пин-пада).
5. `FormId = 13` (`pgPrint`, `Order = 5`) — Окно фискализации и печати чека.

### 1.3. Параметры платежа (tb_TspPayParameters и tb_TspPayParamParameters)
В `tb_TspPayParameters` для прототипа заводятся параметры транзакции:
- `Code = 1` (`Номер лицевого счета`): `PayTemplateFormId = 99019001` (связан с первой экранной формой `pgInpCell` для считывания пользовательского ввода).
- `Code = 2` (`Информация по счету`): `PayTemplateFormId = 0` (выходной текстовый параметр, отображаемый на шаге `pgCheckParameters`).
- `Code = 3` (`Сумма к оплате`): `PayTemplateFormId = 0` (числовой параметр суммы платежа).

Правила валидации и обработки в `tb_TspPayParamParameters`:
- `ParamListId = 2` (`Validator`): посимвольная маска ввода (`xxxxxxxxxx`).
- `ParamListId = 3` (`RegExValidator`): регулярное выражение (`^(\d{1,10})$`).
- `ParamListId = 4` (`MaxLength`): максимальная длина строки (`10`).
- `ParamListId = 6` (`InputParameterName`): отображаемый заголовок поля ввода.
- `ParamListId = 8` (`PayParameterCheckerClassName`): имя класса проверки (например, `clsLateraCountChecker` или `clsOSMPChecker`).
- **`ParamListId = 9` (`IsSumOfPayment = TRUE`): критически важный параметр!** Если ни один параметр ТСП не помечен этим флагом, терминал не знает, в каком поле лежит сумма транзакции, и при переходе к оплате картой пин-пад получит `0 руб.`

### 1.4. Правило чека (tb_TspCheques)
- `TspId = 99019`, `ChequeId = 1`, `Order = 1`.

---

## 2. Специфика флоу ТСП оплаты по API ОСМП

Интеграция терминала с внешним биллингом (в том числе развернутым в облаке или в `etranprocessing`) основана на стандарте протокола `OSMP` (`QIWI / e-port`).

### 2.1. Конфигурация соединения (ExternalVariable.xml)
Параметры подключения к API хранятся в файле:
`<КаталогТерминала>\Custom\ExternalVariable.xml`

Формат XML (соответствует схеме `ADO.NET DataSet`):
```xml
<?xml version="1.0" standalone="yes"?>
<DocumentElement>
  <ExternalVariable>
    <Name>URL</Name>
    <Value>http://127.0.0.1:8008/osmp</Value>
  </ExternalVariable>
  <ExternalVariable>
    <Name>Login</Name>
    <Value></Value>
  </ExternalVariable>
  <ExternalVariable>
    <Name>Password</Name>
    <Value></Value>
  </ExternalVariable>
</DocumentElement>
```
Чтение значений в коде выполняется через синглтон:
`clsVariable.Instance.GetExternalParameterByName("URL")`.

### 2.2. Этап 1: Проверка счёта (command=check)
1. Вызывается сразу после ввода номера счета на форме `pgInpCell` при нажатии «Вперед».
2. Формируется запрос проверки:
   `GET / POST` на `<URL>?command=check&txn_id=0&account=<номер_счета>&sum=0.00`
   - Для защищенных облачных шлюзов при наличии `Login`/`Password` передается HTTP Basic Auth.
   - Поддерживается SSL/TLS через клиент `clsWeb.RequestSSL`.
3. Ожидаемый формат ответа сервиса:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<response>
    <osmp_txn_id>0</osmp_txn_id>
    <result>0</result>
    <comment>OK</comment>
    <sum>120.00</sum>
    <balance>120.00</balance>
    <recipient_name>Лицевой счет: 5555, Сумма к оплате: 120.00 руб.</recipient_name>
    <fields>
        <field1>Счет: 5555</field1>
        <field2>К оплате: 120.00 руб.</field2>
    </fields>
</response>
```
4. Логика обработки чекером (`clsLateraCountChecker`):
   - Если `response/result != "0"`: извлекается `response/comment`, выводится на экран как сообщение об ошибке, переход блокируется (`PayParameterNotValid`).
   - Если `response/result == "0"`:
     - Извлекается текст из `recipient_name` и записывается в параметр `Code = 2` (`Информация по счету`).
     - Извлекается сумма из `<sum>` или `<balance>`, парсится в `decimal`.
     - **Сумма устанавливается в свойство ядра:** `clsPaymentPagesManager.Instance.PayCardSum = parsedSum`.
     - Сумма записывается в параметр с признаком `IsSumOfPayment = TRUE` (`Code = 3`).

### 2.3. Этап 2: Выбор способа оплаты и приём средств
- Пользователь подтверждает реквизиты на `pgCheckParameters`.
- На форме `pgSelectPayType` выбирается тип платежа:
  - **Наличные:** переход на `pgGetMoney`. Купюроприемник принимает физические банкноты, инкрементируя внесенную сумму.
  - **Карта:** форма `pgGetMoney` вызывает инициализацию пин-пада:
    `B.Classes.clsDeviceManager.Instance.BeginPayment(Convert.ToInt32(B.Classes.clsPaymentPagesManager.Instance.PayCardSum * 100));`
    Сумма передается в копейках целым числом (`int`).
- Пин-пад выполняет авторизацию через эквайринг и возвращает код успешности (`ResultCode == 0`).

### 2.4. Этап 3: Проведение платежа (command=pay)
После фиксации внесенной суммы ядро терминала инициирует транзакцию проведения:
- `command=pay&txn_id=<terminal_txn_id>&txn_date=<YYYYMMDDHHMMSS>&account=<номер_счета>&sum=<сумма_в_рублях>`
- При успешном ответе шлюза (`result == 0`) печатается фискальный чек (`pgPrint`).

---

## 3. Специфика точечной сборки изменений в PlaterraTerminal

При необходимости внести изменения только в одну библиотеку (например, `Platerra.Terminal.BLL.dll`) без пересборки всего громоздкого решения и без обновления сопутствующих DLL на клиенте действуют строгие правила.

### 3.1. Команда изолированной сборки одного проекта
- **Среда сборки:** Visual Studio 2022 Community (MSBuild 17.x).
- **Путь к компилятору:** `d:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\bin\MSBuild.exe`
- **Команда:**
```powershell
& 'd:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\bin\MSBuild.exe' `
    'D:\repo\platerra\Public\PlaterraSoft\Terminal\Platerra.Terminal.BLL\Platerra.Terminal.BLL.csproj' `
    /t:Rebuild /p:Configuration=Debug /p:BuildProjectReferences=false /v:minimal
```
Параметр `/p:BuildProjectReferences=false` предотвращает нежелательную пересборку и рассинхронизацию проектных ссылок.

### 3.2. Подводные камни бинарной совместимости
1. **Несоответствие версий внешних сборок (Assembly Binding):**
   - В рабочей папке терминала `D:\Platerra26` развернуты:
     - `Newtonsoft.Json.dll` версии **9.0.0.0** (а не 13.x).
     - `System.Net.Http.dll` версии **2.2.29.0** (а не 4.x).
   - Если в `Platerra.Terminal.BLL.csproj` подключены более новые NuGet-пакеты, при старте `PlaterraTerminal.exe` возникает `System.TypeLoadException`.
   - Решение: в `csproj` ссылки должны быть привязаны к версиям из папки терминала, а в `D:\Platerra26\PlaterraTerminal.exe.config` в секции `<assemblyBinding>` должны присутствовать `<bindingRedirect>`:
```xml
<dependentAssembly>
  <assemblyIdentity name="Newtonsoft.Json" publicKeyToken="30ad4fe6b2a6aeed" culture="neutral" />
  <bindingRedirect oldVersion="0.0.0.0-13.0.4.0" newVersion="9.0.0.0" />
</dependentAssembly>
<dependentAssembly>
  <assemblyIdentity name="System.Net.Http" publicKeyToken="b03f5f7f11d50a3a" culture="neutral" />
  <bindingRedirect oldVersion="0.0.0.0-4.2.0.0" newVersion="2.2.29.0" />
</dependentAssembly>
```

2. **Зависимость от неразвёрнутых типов (TypeLoadException при старте):**
   - Если в коде `BLL` используются типы, которых нет в развернутых DLL в `D:\Platerra26` (например, `Platerra.Terminal.Common.Enums.WorkModes` или `Platerra.Terminal.DAL.Mail.clsLeo4MailSender`), среда выполнения `.NET` аварийно завершит приложение при попытке загрузить класс.
   - Правило: код точечно пересобираемой DLL должен использовать **только те типы и сигнатуры**, которые физически присутствуют в развернутых библиотеках (`Platerra.Terminal.Common.dll`, `Platerra.Terminal.DAL.dll`).

3. **Скрытый сбой инициализации устройств:**
   - Если в начальном инициализаторе (`clsLogInitializer`) падает `TypeLoadException`, метод `clsInitializer.BeginInitialize()` аварийно завершает всю цепочку.
   - В результате шаг № 3 (`clsDeviceInitializer`) **не выполняется**, и объект `PinPad` остается `null`.
   - Терминал запускается визуально нормально, но при попытке оплатить картой в `clsDeviceManager.BeginPaymentThread` возникает `System.NullReferenceException: Ссылка на объект не указывает на экземпляр объекта`.

4. **Блокировка DLL операционной системой Windows:**
   - Если `PlaterraTerminal.exe` запущен, файл `Platerra.Terminal.BLL.dll` заблокирован в памяти. Команда `Copy-Item -Force` не сможет перезаписать его (останется старая сборка).
   - Для надежного развертывания без конфликтов:
     - Завершить процесс `PlaterraTerminal.exe`, либо
     - Использовать специфику NTFS: переименовать заблокированный файл (`Move-Item Platerra.Terminal.BLL.dll Platerra.Terminal.BLL.old`), после чего скопировать новую сборку.
