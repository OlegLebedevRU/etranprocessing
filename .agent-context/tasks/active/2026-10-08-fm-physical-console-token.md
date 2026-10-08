# FM physical console token selection

## Контекст и границы
Working tree 55a5, tools/l4con; явное решение пользователя — адаптивный выбор
токена physical console user для UAC-off/autologon admin. Владелец — native FM.
MQTT extra_service, lease, file paths/private exclusions, ACL и write capabilities
не менялись. Применены repo intake/native Windows skills.

## Изменение
fm_user.h предпочитает Limited токен той же console SID/session. Full требует
проверенную Limited пару; Default с доступной парой также выбирает Limited.
Только Default без пары (ERROR_NO_SUCH_LOGON_SESSION) сохраняет собственные
права пользователя, включая nonsplit admin. AccessDenied/неизвестные ответы,
чужая SID/session, service identity и повторный logon/session drift — отказ.
Возвращаемый AuthenticationId остаётся действующим FM fence.

## Проверки и evidence
- Unified `tools/l4con/build.cmd all`: exit0, x86/x64/default, существующие FM
  private receipt/path/alias/atomic rename, drain и owned Job tests PASS.
- `tests/test_fm_user.c`: 20 checks, 0 failures x86 и x64. WinAPI результаты
  token-selection смоделированы; это не live UAC-off acceptance.
- `tests/test_rpc_runtime.cmd`: exit0, x86/x64; реальный private IPC drain,
  зарегистрированный7032/event76 и orphan REQ/EVT через существующий клиент PASS.
- Default exe SHA256 равен x86 exe.
- Авторизованный read-only hidden SYSTEM WTS probe текущей машины:
  console session1, primary Limited, primary_admin=false, linked Full,
  adaptive accepted nonadmin; EnableLUA=1. Старая !admin проверка этот токен
  не блокирует. Текущая ошибка live FM этим кейсом не воспроизведена.
  Санитизированный локальный evidence — ignored obj/fm-token-probe/result.json.

## Не выполнено / cleanup
- Live UAC-off/autologon admin E2E не выполнялся; настройка UAC не менялась.
- Подписанный deploy и повторный реальный download/write FM после него остаются
  отдельной проверкой владельца. Установленные бинарники/службы не изменены.
- Созданные unique SYSTEM task/private PD probe assets удалены; локальные
  ignored build/probe outputs сохранены для диагностики. SID/секреты не выводились.
- Component card и file-manager contract уточнены; модельный результат не
  объявляется доказательством текущего live failure или security E2E.
