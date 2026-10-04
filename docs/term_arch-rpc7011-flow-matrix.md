# RPC7xxx / RPC7011: реализованный flow и границы надёжности

Состояние на 2026-10-05: suite1.11.0 подписан и опубликован; установка773
подтверждена оператором. Production PB schema030 и совместимые backend/IoT
развёрнуты; статус frontend и E2E issuance — в release handoff.
Компоненты: l4con 1.10.0, l4pin 1.8.0, leo4proxy 1.8.2; shared schema 030.

## Владельцы и переходы контрактов

| Шаг | Стек / владелец | Фактический результат для следующего стека |
|---|---|---|
| 1 | IoT: задачи и диагностика | [7xxx fixture](contracts/rpc7xxx-gate1.json), method-aware `payload.dt`, metadata результата, безопасный cancel lifecycle |
| 2 | Native: l4con / leo4proxy | Строгий consumer fixture; дедупликация TSK/RSP; status/result_uid; ограниченное исполнение и локальный запрет при истечении сертификата |
| 3 | PB / shared | [API fixture](contracts/pb-renewal-gate3.json), schema 030, purpose=renew, authenticated CHECK/SETUP и сохранённое восстановление |
| 4 | IoT: 7011 | [7011 fixture](contracts/rpc7011-gate4.json), TTL до PIN expiry, capability poll, разделение доставки PIN и маскированной истории |
| 5 | Native: l4con / l4pin | Фиксированный handler 7011, PIN stdin, проверка Job, protected busy, DPAPI CSR recovery |
| 6 | MenuBuilder | Синхронные PIN + queue; только настоящий IoT task_id; отдельная форма и асинхронная сверка |
| 7 | Release / acceptance | Подпись обоих payload и setup, migration/compatible services, обновление агента и отдельная E2E проверка |

Обёртка `payload={"dt":[...]}` обязательна. Тип элемента и число элементов задаёт
метод. 7001 требует один exec item; 7002 принимает пустой массив или один item
с session_id; 7003 принимает пустой массив; 7011 требует один renew item с PIN,
UTC epoch `pin_expires_at` и `ttl_sec=120`. Входные поля проверяются строго.
7004/7005 не становятся реализованными командами из-за принятия пустой обёртки:
текущий l4con отвечает 501. Capability poll объявляет только 7001/7002/7003/7011.

## Последовательность продления

1. Оператор выбирает **«Заказать удалённое продление»**. Старое «Создать PIN»
   остаётся отдельным setup flow. Новый PIN никогда не возвращается браузеру.
2. MenuBuilder проверяет tenant/admin, административный статус, живой new-CA
   сертификат, действующие права бесплатного первого / платного терминала.
   Classic сохраняет существующую политику допуска. Offline не препятствует очереди.
3. PB под блокировкой terminal выдаёт purpose=renew PIN либо возвращает прежний
   незавершённый PIN. Использованный PIN переиспользуется только в 15-минутном
   recovery window, с сохранённым PKCS7 и историей именно текущего выпуска.
4. MenuBuilder ставит 7011 непосредственно в IoT и ждёт только ответа постановки.
   IoT создаёт task_id. Ни предварительного UUID, ни нового operation_id/outbox нет.
   Известный принятый task возвращается даже при сбое вторичного audit write.
5. При потерянном ответе исход неопределённый. Форма разрешает три ручных повтора
   с тем же pin_id/PIN, затем закрывается с рекомендацией повторить через 3 минуты.
   После повторного открытия PB также находит прежний PIN. Это не автоматические retries.
6. После подключения / каждые 60 секунд простоя l4con делает штатный zero-UUID
   REQ с capability whitelist. 7011 ждёт RSP; TSK не содержит PIN.
7. l4con запускает установленный sibling l4pin напрямую в Job, без shell и PATH
   поиска, передаёт PIN через stdin. Job получает защиту от cancel-and-replace.
   Следующая exec/renew возвращает busy 409; FIFO и ожидания предыдущей установки нет.
8. l4pin проверяет разрешение живого Job у l4con, берёт общий non-waiting mutex
   установки. Конкурирующий installer отказывается сразу. Прямой запуск режима
   извне l4con не получает разрешения. Удалённая консоль допустима при достаточном TTL.
9. CHECK/SETUP идут JSON POST в защищённый PB renew route через localhost proxy
   или строгий HTTPS mTLS. Legacy setup не принимает renew PIN. Нет anonymous retry,
   игнорирования сертификата сервера, PIN в URL или автоматического downgrade.
10. До SETUP сохраняются CSR и имя non-exportable CNG key; до изменения cert store
    сохраняется публичный PKCS7. Защита: DPAPI machine + SYSTEM/admin registry ACL.
    Потерянный ответ повторяется с тем же CSR; DB хранит ответ вместе с новым serial.
11. Установка завершает RPC обычным RES/CMT. Proxy подхватывает новый сертификат;
    событие 75 сохраняет inventory semantics. Форма не ждёт терминал онлайн.
12. Кнопка проверки читает PB PIN/discovery и при известном task_id один IoT detail.
    `used` означает выпуск, а не доказанную установку. `confirmed` требует свежего
    успешного mTLS discovery с текущим serial после used_at. Фонового UI polling нет.

## Бюджеты и последовательность

| Участок | Фактическое ограничение | Следствие |
|---|---|---|
| MenuBuilder dispatch | общий 25 s; service HTTP 10 s | Не ждёт исполнение; timeout после POST может означать принятую задачу |
| Очередь IoT | минимум RPC TTL / PIN UTC expiry | Стандартный PIN 24 h; это не 7 суток |
| Выбор TTL в BFF | floor((PIN remaining − 5 s) / 60), максимум 44639 min | Запас передачи; IoT дополнительно ограничивает expires_at |
| Допуск исполнения 7011 | PIN remaining строго больше 120 s | Задача в очереди может быть отвергнута ближе к expiry |
| Idle queue poll | 60 s; только без выполняющейся команды | Активный exec задерживает получение, вплоть до истечения PIN |
| l4con Job | 120 s; общий CLI timeout 1–3600 s | Включает запуск, IPC, l4pin и cleanup; CLI 0/garbage отклоняется |
| IPC grant | минимум 100 s остатка Job | Для l4pin 90 s остаётся ≥10 s запаса; поздний запуск отказывается |
| l4pin | внутренний бюджет 90 s | WinHTTP синхронный; внешняя Job граница 120 s покрывает зависание |
| PB renew | общий 60 s; CA call ≤60 s и PIN remaining | CA HTTP по умолчанию 30 s; последующий код повторно проверяет PIN expiry |
| Local MQTT TCP | connect 5 s, CONNACK 10 s, frame 10 s, send 3 s/packet | Частичные пакеты не продлевают абсолютный deadline |
| Normal exec replacement | join ≤5 s | Если worker не завершился, busy; protected renewal не отменяется |
| Shutdown | join ≤15 s, затем failsafe exit | Job kill-on-close; контекст живого worker не освобождается |
| Cert hot rotation | proxy scan 30 s; event75 scan 60 s / retry 15 s | Сервисы для обычного renew не перезапускаются |
| Policy expiry | admission немедленно; worker до 25 s fetch + 1 s | Нет ожидания PB policy grace для отсутствующего/истёкшего cert |
| Обычный policy refresh | 600 s | Использует загруженный cert expiry; отдельный агрессивный DB timer не нужен |
| Same-CSR recovery | ≤15 min от used_at и до PIN expiry | Тот же выпуск; superseded certificate и другой CSR отклоняются |

Бюджеты вложены, их нельзя просто сложить и назвать SLA. Queue dwell / офлайн
не входят в 120 s исполнения. До grant запуск может потратить максимум 20 s,
иначе операция отказывается вместо старта без запаса. Фоновая сверка зависит
от следующего реального PB ingress/discovery; 600 s не гарантирует подтверждение
при сетевой аварии. Настроенный произвольный MQTT hostname всё ещё использует
синхронный gethostbyname: DNS ceiling для него не гарантирован. Штатный literal
127.0.0.1 / localhost обходится без DNS; remote resolving выполняет leo4proxy.

## Матрица вариантов / отказов

| Вариант | Ожидаемый исход / восстановление |
|---|---|
| 7001 с корректным item / без payload | Исполнение после RSP / отказ валидации |
| 7002 empty, payload_required=false | Отмена текущей команды по TSK; REQ/RSP/RES без повторной отмены |
| 7002 session_id | Ждёт RSP; другой session →404; ответ cancel_requested не равен факту завершения |
| QoS duplicate TSK/RSP | В пределах 64-task runtime cache нет повторного exec; повтор RES с прежним result_uid |
| Перезапуск агента | Cache потерян: нет обещания exactly-once exec; renew защищён PIN/CSR/DB/mutex |
| 7011 во время обычного exec | Штатный cancel-and-replace exec; join≤5 s, затем запуск или busy; FIFO нет |
| Exec / другой renew во время protected renew | busy 409, предыдущий процесс не отменяется |
| Одновременно два installer / GUI / legacy | Один mutex owner; другой сразу отказывается |
| Явный cancel / shutdown / Job timeout | Может прервать renew; pending CSR/PKCS7 сохраняет возможность recovery |
| Старый агент без7011 capability | Новая idle poll не доставляет7011; явная доставка может дать501; требуется обновление tools |
| Терминал offline | Queue до TTL/PIN expiry; UI не требует online result |
| PIN успел истечь / остаток≤120 s | Новое исполнение запрещено; ручной новый заказ после восстановления |
| Admin block / нет прав / expired cert | Отказ допуска; PB повторно проверяет на выдаче и установке |
| Потерян ответ queue | Ручной повтор того же PIN; duplicate task допустим |
| Потерян ответ SETUP после commit | Тот же CSR возвращает сохранённый PKCS7 без повторной CA issuance |
| Ответ получен, store install interrupted | Сохранённый PKCS7 / тот же key позволяют продолжить |
| Used PIN, другой CSR / другой выпуск | Отказ до CA и serial write; никаких новых business effects |
| Потеря ответа дольше recovery window | Автоматического восстановления не обещаем; операторский legacy recovery |
| Policy недоступна, valid cert | Сохраняется существующий 72 h offline grace |
| Policy недоступна, cert expired/missing/not-yet-valid | Локальный deny MQTT/RTP; HTTPS policy/diagnostics остаются по действующим правилам |
| RES потерян после установки | Свежий PB mTLS discovery может подтвердить выпуск без успешного RPC результата |
| Audit write недоступен после queue accepted | UI получает известный task_id; автоматическое восстановление audit не вводится |

Надёжность описана переходами и границами, не придуманным процентом доступности.
2304 существующих endpoint/DNS cases — отдельная enumerated regression matrix,
а не покрытие всего декартова произведения renewal × сеть × UI × DB.

## Проверки и открытые acceptance gates

- IoT: 523 passed / 7 skipped Windows,497 passed /33 skipped builder Linux; PB: 176 passed; shared: 65 passed.
- MenuBuilder backend:626 passed /20 skipped; frontend tsc/Vite и7 DTO/redaction tests прошли.
- Native x86/x64/default builds; runtime cancel/dedup/deadline, Job authority,
  DPAPI recovery, user-event IPC, policy expiry и resolving regressions прошли.
- Миграция 029→030 прошла на production PostgreSQL; head030 подтверждён.
  Конкуренция транзакций разных workers, настоящий CA response-loss/store-install,
  перезагрузка recovery и Windows 7 runtime ещё не проверены.
- Legacy CA client сохраняет прежний `verify=False`; это отдельный существующий
  trust debt между PB и CA, не отменяющий строгий terminal-facing mTLS renew route.
  CA issuance и PB commit не являются одной транзакцией: потеря результата CA
  до commit остаётся неоднозначной и требует отдельной проверки/операторского разбора.
- Production activation только после schema030 и совместимых consumers, проверки
  подписанного tools 1.11.0 на целевом устройстве и E2E принятия 7011. Не считать
  прежний ready/0 на773 с1.10.2 доказательством этого нового flow.

## Runtime correction at contract boundary

A real PostgreSQL task-detail query exposed JSONB non-hashable uniqueness after
adding redacted history payload. IoT8c2be80 deduplicates by task_id instead of the
whole row. An actual SQLAlchemy ORM JSON result/join regression was added; Linux
builder tests and real historical task/detail reads are required after rollout.
This source correction does not change signed native components.
