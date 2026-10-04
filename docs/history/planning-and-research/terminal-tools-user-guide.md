# Руководство пользователя Leo4 Tools: актуальный маршрут

Для signed tools **1.10.1** используйте
[руководство инженера](term_tool-user-guide.md): состав выпуска, скачивание,
подпись/SHA-256, установка и upgrade через `l4setup.exe`, настройки сети,
TLS probes, итоговые статусы и rollback.

Краткий старт: [tools/USER_GUIDE.md](../tools/USER_GUIDE.md).
Proxy: [руководство оператора](../tools/leo4proxy/USER_GUIDE.md).
Разработчикам: [интеграция](term_tool-developer-guide.md) и
[архитектура](term_tool-architecture-guide.md).

Прежняя инструкция с `l4install_x86.cmd`/`l4install_x64.cmd`, `tools.zip` и
отдельным `ffmpeg.zip` описывала старый ZIP-дистрибутив. Для опубликованного
1.10.1 достаточно подписанного `l4setup.exe` со встроенными payload x86/x64.
Сам установщик x86, payload выбирается по архитектуре целевой Windows.
Legacy l4install сохранён в коде для совместимости; он не является основным
маршрутом текущего релиза.
