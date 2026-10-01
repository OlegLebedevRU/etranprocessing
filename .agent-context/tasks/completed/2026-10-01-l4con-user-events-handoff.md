# l4con 1.9.3: пользовательские события 900–999

## Task intake / scope
- Цель: quiet CLI → local IPC → running l4con extra_service → existing MQTT5
  dev/{SN}/evt → IoT collector. extra_service подтвержден пользователем.
- Владелец: l4con формирует событие, app1 сохраняет его; БД/миграции не меняются.
- Scope: tools/l4con, документация консоли/l4mcp, карточка l4con, этот handoff.
  Другие tools/code backends не изменены; предыдущая работа leo4proxy и сторонние
  untracked пользовательские файлы сохранены. Без нового пакета l4tools.
- User authorization: изменить/оставить installed exe, отправлять9xx только773,
  использовать временные MCP/IoT ключи. Stop/start служб выполнял пользователь.
- Working tree без commit/push. Source/resource1.9.3 (previous source1.9.2,
  previous installed resource1.7.2.0). Windows7-compatible x86/x64 static/MT.

## Контракт / инварианты
| Producer → consumer | Канал / поля | Результат |
|---|---|---|
| Remote7001 descendant → l4con | local-only pipe, v1 bounded record; PID from Windows + IsProcessInJob | CLI0/2–6, no queue/retry/refusal logs |
| l4con → collector | existing dev/{SN}/evt,101/102/200/300/correlationData;446 raw text,447 int32,448 optional externalUUID | QoS1/retain0, no new MQTT connection/presence |
| console_run → remote executor | existing exact-device preflight/access/lease/7001 | normal results and lease release |

- Code900–999 default999. Text<=1024 UTF-8 bytes before JSON escaping; overflow
  omits446 and overrides447=-2. 447 default0, invalidint32=-1. Missing448 omitted;
  supplied invalidUUID rejects entire callCLI2. Internal correlationData stays separate.
- Only members of the active command Job/TTL can send; no client-supplied PID/session
  trust, no breakaway. One attempt/1000ms globally, no burst/queue/autoretry.
- Root and descendants share TTL; when root exits first, wait for remaining children.
  Cancel/output limit/TTL/end revoke the grant and terminate the owned Job.
- Worker uses one shared context, join before reuse, synchronized socket send/close.
  PowerShell uses EncodedCommand: real initial testing exposed and fixed quote loss
  in previous unquoted Command launch. Quotes/overflow regression tests added.

## Проверки
- [x] Final build.cmd all: x86/x64/default, existing MQTT5 tests, no compiler warnings.
- [x] test_user_events.cmd: both architectures /W4 /WX /MT; int32/UUID/UTF8/escaping,
  denied local caller, descendant grant, rate limiter, revoke/expiry/cancel, root exits
  before descendant, actual PowerShell quoted arguments/oversized payload.
- [x] git diff --check, scoped read-only secret scan0 matches, documentation links,
  CLI quiet negative checks, architecture/resource/version/hash checks.
- [x] Installed final x64 after user stopped L4Superv/L4Con; verified Stopped/PID0/no
  l4con process. User started both again; final L4Con PID17120/L4Superv PID217248.
- [x] Real MCP773 preflight/console_run with user-supplied temporary credential.
  Configured MCP principal denied773; no authorization bypass or config change.
- [x] IoT read-only GET /api/v1/device-events/ with temporary x-api-key, only773/testcodes.

| Сервер / слой | Факт |
|---|---|
|991 id1356933|446 exact JSONtext,447 integer7,valid448|
|992 id1356934|447=-1,absent448|
|993 id1356938|oversized1025 omitted446,447=-2,preserved448|
|994|invalidUUID CLI2,no server event|
|995 id1356939|five rapid CLI calls0,5,5,5,5; exactly one final-test UUID event|
|999 id1356940|x86CLI→x64service,defaultcode999,447=INT32_MIN|
|996 id1356941|Unicode preserved,447=INT32_MAX|
|997 id1356942|exact512 Cyrillic chars=1024 UTF8bytes preserved,447=7|
|Local after remote end|x86/x64 denied3,empty stdout/stderr,no stale grant|

- Initial pre-fix PowerShell tests failed quote parsing; corrected/retested successfully.
  Initial storm995 had two events1013ms apart; final storm uses a distinct448 and
  confirms exactly one event for the clean five-call test.
- No remaining child processes under final L4Con; one established localhost1883 connection.
  Leo4Proxy PID183528/mosquitto PID218760 unchanged; SN/thumbprint/ready/routes_active preserved.

## Артефакты / cleanup
- Installed/retained C:\l4tools\l4con\l4con.exe final x64 SHA256
  874c7da1ae285bc364f3a6ee2e2514fd143aa994d6e6c18a2791687010240ce1.
- x86/default SHA256 da3c9eb97bb72246c2371a5f0c2bbbef8a3c47bd94c9e5b44f0029c4538bb15e.
- Original backup C:\l4tools\l4con\l4con.exe.before-1.9.3-20261001-141809;
  originalx64 SHA256 e29fb2569f7ef4df2460ed5b6e2404e61bd4092933e0f77878e7da3d03eee947.
- Evidence (ignored tools/l4con/obj): runtime-events-mcp-results.json,
  runtime-events-server-{before-fix,final}.json, runtime-events-acceptance.json,
  runtime-events-{baseline,local-checks,final-services}.json.
- Temporary credential-bearing environment values used only per invocation, no
  tracked secret/config changes. Temporary HTTP helper/call input/schema removed.
  User revokes both temporary keys after testing; do not claim revocation performed.
- [User documentation](../../../docs/term_tool-l4con-user-events.md), console protocol,
  l4mcp architecture, component card and navigation updated.

## Ограничения
- Windows7 compiled, not runtime tested. Failed Job assignment denies event access
  while leaving the ordinary remote command available.
- Local administrator/SYSTEM are outside this local access boundary.
- CLI0 means socket publication, not PUBACK/collector acknowledgement; real server
  storage was independently confirmed for test events via API.
- Backend/MCP/frontend source and suite installer/package not changed; no backend
  pytest/ruff/pyright/npm checks were run for this native/docs change.
