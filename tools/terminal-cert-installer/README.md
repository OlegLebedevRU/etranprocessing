# Etran Terminal Certificate Installer (C / CNG / Win32)

Автономная нативная CLI-утилита для Windows, реализующая процедуру выпуска и установки сертификата терминала по PIN-коду (флоу `v=26` / CNG KSP / RSA 2048) с **запретом экспорта закрытого ключа** и автоматической очисткой старых сертификатов по совпадению `email`.

---

## Возможности

- **Флоу `v=26` (CNG / KSP):**
  - Шаг 1: `GET /api/certificates/?function=check&pin=<PIN>&tosign=<sysinfo>&v=26`
  - Шаг 2: Генерация неэкспортируемого ключа RSA 2048 в `Microsoft Software Key Storage Provider` (`NCRYPT_ALLOW_EXPORT_NONE = 0`).
  - Шаг 3: Формирование PKCS#10 CSR с подстановкой `CN=<sign>`.
  - Шаг 4: `POST /api/certificates/?function=setup&pin=<PIN>&cpserial=<sign>` с телом CSR.
  - Шаг 5: Декодирование PKCS#7 цепочки и установка сертификата в системное хранилище Windows.
- **Безопасность закрытого ключа:**
  - Закрытый ключ генерируется внутри защищенного хранилища Windows (CNG KSP) и помечается как неэкспортируемый.
  - Невозможно извлечь ключ через экспорт сертификата в файл (`.pfx`/`.key`).
- **Очистка устаревших сертификатов:**
  - Перед установкой нового сертификата удаляет из целевого хранилища (`LocalMachine\MY`) все ранее установленные сертификаты с совпадающим адресом `email` (`новый_серт.email == старый_серт.email`).
- **Zero Dependencies:**
  - Нативный бинарник без зависимостей от сторонних DLL, .NET или Python runtime.
  - Статическая линковка CRT (`/MT`).
- **Поддержка Schannel / mTLS:**
  - Корректная привязка CNG-ключа к контексту сертификата с `dwKeySpec = 0` (в `CRYPT_KEY_PROV_INFO`), что обеспечивает полную совместимость с Windows Schannel, .NET `HttpWebRequest` и mTLS Nginx (:4443 / :443) без ошибок `SEC_E_UNKNOWN_CREDENTIALS` (`0x8009030D`).

---

## Архитектура и интеграция

Подробная спецификация подсистемы сертификатов, правил валидации в БД и дорожной карты развития C-инструментов для терминалов описана в:
👉 **[`docs/certificate-architecture.md`](../../docs/certificate-architecture.md)**

### Стратегия развития C-инструментов для интеграции с новым бэкендом:
1. **`terminal-cert-installer.exe` (текущий инструмент):** автономный CLI для первоначальной и плановой установки/замены сертификатов.
2. **`libterminal-crypto.dll` (планируется):** компактная C-библиотека для бесшовного вызова из легаси C#/.NET 4.0 ПО терминалов через P/Invoke.
3. **`terminal-sidecar.exe` (планируется):** фоновая служба/демон на C для фонового автопродления сертификатов ($\le 30$ дней), передачи телеметрии и синхронизации локального кеша меню.

---

## Сборка (Zero-Dependency & Unified 32/64 Архитектура)

Утилита полностью поддерживает как **32-битные (x86)**, так и **64-битные (x64)** версии Windows.

### Вариант 1: Через командную строку (MSVC Build Tools 2022)
Скрипт `build.cmd` автоматически компилирует статически слинкованные бинарники под обе архитектуры (x86 и x64):
```cmd
cd tools\terminal-cert-installer
build.cmd
```
* **Параметры сборки:**
  - `build.cmd` (или `build.cmd all`) — собирает обе архитектуры (x86 и x64).
  - `build.cmd x86` — собирает только 32-битную версию.
  - `build.cmd x64` — собирает только 64-битную версию.
* **Результаты сборки в каталоге `bin/`:**
  - `bin\x86\terminal-cert-installer.exe` — 32-битный нативный бинарник (универсален: работает на 32-битных ОС POSReady 7 и на 64-битных через WOW64).
  - `bin\x64\terminal-cert-installer.exe` — 64-битный нативный бинарник.
  - `bin\terminal-cert-installer.exe` — стандартный исполняемый файл по умолчанию.

### Вариант 2: Через CMake / CLion (MinGW или MSVC)
```powershell
# Сборка x64
& "C:\Program Files\JetBrains\CLion 2025.2.4\bin\cmake\win\x64\bin\cmake.exe" -B build -G Ninja -DCMAKE_C_COMPILER="C:/Program Files/JetBrains/CLion 2025.2.4/bin/mingw/bin/gcc.exe" -DCMAKE_MAKE_PROGRAM="C:/Program Files/JetBrains/CLion 2025.2.4/bin/ninja/win/x64/ninja.exe"
& "C:\Program Files\JetBrains\CLion 2025.2.4\bin\cmake\win\x64\bin\cmake.exe" --build build --config Release

# Сборка x86 (MSVC Win32)
cmake -B build32 -A Win32
cmake --build build32 --config Release
```

---

## Использование

### 1. Установка сертификата по PIN-коду (по умолчанию LocalMachine\MY):
```cmd
terminal-cert-installer.exe B75GL9
```

### 2. Установка с явными параметрами:
```cmd
terminal-cert-installer.exe --pin B75GL9 --url https://iot-processing.ru/api/certificates --store machine
```

### 3. Просмотр установленных сертификатов в хранилище:
```cmd
terminal-cert-installer.exe --status
```
