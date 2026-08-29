# l4sql - Leo4 Terminal SQL Client Utility

Нативный высокопроизводительный клиент для прямого выполнения запросов к MS SQL Server из удаленной веб-консоли `l4con` или локальной командной строки Windows.

---

## Возможности

1. **Автообнаружение конфигурации терминала (Zero-Config)**:
   - Автоматически сканирует диски на наличие папок `*platerra*`, `*postomat*`.
   - Находит наиболее актуальный по времени изменения лог `PlaterraTerminal.log`.
   - Загружает и парсит `DBConfig.xml` (сервер, имя БД, тип авторизации).
2. **Аутентификация Windows / SQL**:
   - По умолчанию подключается с учетной записью текущего пользователя Windows (SSPI / `Trusted_Connection=yes;`).
3. **Безопасность (Read-Only Guard + SP Protection)**:
   - Блокирует любые деструктивные запросы (`UPDATE`, `INSERT`, `DELETE`, `DROP`, `ALTER`, `TRUNCATE`, `SELECT INTO`).
   - Разрешает выполнение только хранимых процедур с безопасным префиксом `l4_` (`EXEC l4_...` или `EXEC dbo.l4_...`).
4. **Удобные шорткаты**:
   - `l4sql tb_Variables` -> `SELECT * FROM tb_Variables` (с безопасным лимитом строк).
   - `l4sql select tb_Variables` -> `SELECT * FROM tb_Variables`.
5. **Форматы вывода**:
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
