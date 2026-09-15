# l4sql - Leo4 Terminal SQL Client Utility

Нативный высокопроизводительный клиент для прямого выполнения запросов к MS SQL Server из удаленной веб-консоли `l4con` или локальной командной строки Windows.

---

## Возможности

1. **Автообнаружение конфигурации терминала (Zero-Config)**:
   - Автоматически сканирует диски на наличие папок `*platerra*`, `*postomat*`.
   - Находит наиболее актуальный по времени изменения лог `PlaterraTerminal.log`.
   - Загружает и парсит `DBConfig.xml` (сервер, имя БД, тип авторизации).
2. **Аутентификация Windows / SQL и адаптивный уровень доступа**:
   - По умолчанию подключается с учетной записью текущего пользователя Windows (SSPI / `Trusted_Connection=yes;`).
   - **Адаптивный конвейер аутентификации**: при запуске из-под фоновой службы `l4con` (`NT AUTHORITY\SYSTEM`) при возникновении ошибки доступа (4060/18456) автоматически определяет активную консольную сессию киоска (`WTSGetActiveConsoleSessionId`), извлекает токен пользователя (`WTSQueryUserToken` с поддержкой `TokenLinkedToken` / High IL на Windows 10), выполняет временную имперсонацию потока и успешно подключается к БД.
   - **Самовосстановление прав (`-G, --grant-system-access`)**: однократная безопасная выдача прав `db_datareader` для `NT AUTHORITY\SYSTEM` (по кросс-языковому SID `S-1-5-18`) в целевой базе данных.
3. **Безопасность (Read-Only Guard + SP Protection)**:
   - Блокирует любые деструктивные запросы (`UPDATE`, `INSERT`, `DELETE`, `DROP`, `ALTER`, `TRUNCATE`, `SELECT INTO`).
   - Разрешает выполнение только хранимых процедур с безопасным префиксом `l4_` (`EXEC l4_...` или `EXEC dbo.l4_...`).
4. **Устранение Mojibake и чистый UTF-8**:
   - Вся диагностика ODBC и данные таблиц извлекаются в Unicode (`SQLGetDiagRecW`, `SQLDescribeColW`, `SQL_C_WCHAR`) и конвертируются в UTF-8, исключая искажение русских символов на Windows 7 SP1 и Windows 10.
   - Корректный подсчет ширины колонок по UTF-8 символам без разрыва многобайтовых последовательностей.
5. **Удобные шорткаты**:
   - `l4sql tb_Variables` -> `SELECT * FROM tb_Variables` (с безопасным лимитом строк).
   - `l4sql select tb_Variables` -> `SELECT * FROM tb_Variables`.
6. **Форматы вывода**:
   - Таблица (по умолчанию), CSV (`--csv`), JSON (`--json` / `-j`).

---

## Использование

```cmd
# Запрос к таблице по короткому имени (лимит 100 строк)
l4sql tb_Variables

# Запрос с указанием лимита строк
l4sql --limit 10 tb_Variables

# Вывод в JSON
l4sql --json --limit 5 tb_Variables

# Произвольный Read-Only SQL запрос
l4sql "SELECT Id, Name, Value FROM tb_Variables WHERE Name LIKE 'sys%'"

# Вызов разрешенной хранимой процедуры
l4sql "EXEC dbo.l4_UpdateSysVersion '3.22.4711.586'"
```

---

## Сборка

```cmd
build.cmd
```
Скрипт компилирует статические бинарники `bin\l4sql.exe`, `bin\x64\l4sql.exe` и `bin\x86\l4sql.exe` без внешних зависимостей.
