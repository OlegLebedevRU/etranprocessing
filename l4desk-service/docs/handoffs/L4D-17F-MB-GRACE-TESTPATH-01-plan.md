# L4D-17F-MB-GRACE-TESTPATH-01 — короткий runtime-сценарий

Статус: `RUNTIME_PASS_WITH_NETWORK_INTERRUPTION`; факты прогона приведены
ниже. Первый сеанс прервала сеть, поэтому stop живого потока проверен на
повторном сеансе.
Сценарий использует существующий tenant 10000 (test05), его основной
терминал 1000007 и дополнительный 1000006. Новые tenant, терминалы и
реальные платежи не создаются.

## Task intake

- Владелец финансовых данных, entitlement и stop outbox: MenuBuilder.
  IoT event feed — producer подтверждённого `device_online`; MenuBuilder
  consumer начисления; IoT session API — consumer команды stop.
- Инварианты: исходный месячный anchor и `grace_deadline` в PostgreSQL не
  меняются; double-entry ledger и source event остаются проверяемыми;
  production commercial flags остаются выключены; тестовое время и tick
  действуют только в изолированном backend для явно разрешённого tenant.
- Неизвестное до прогона: доставка stop до текущего агента, реакция браузера
  на принудительное закрытие, отсутствие ошибочного повторного начисления.

## Подготовка

1. Проверить отсутствие активных сессий tenant 10000 и сохранить snapshot:
   `anchor_at`, cycle 0, `grace_deadline`, balance, projection version,
   monthly charges, последние session IDs и consumer checkpoint. На
   2026-09-27 15:02 UTC anchor = 14:12:02 UTC, deadline =
   2026-09-30 14:12:02 UTC, balance = 1000 коп., monthly charges пусты.
2. Установить тестовый image и два параметра Compose с безопасными
   значениями по умолчанию: пустой tenant allowlist и offset 0. Проверить,
   что production image не менялся и тестовый endpoint возвращает 404 для
   tenant вне allowlist. В коде offset ограничен 604800 секундами.
   Stop outbox должен адресовать текущий video `stream_instance_id` через
   app1 lease/stream API, сверять epoch до stop и закрывать локальную
   сессию штатной функцией metering. Старый вызов remote-session API с
   `stream_instance_id` не подходит для этого потока.
3. Для задолженности требуется **новый настоящий** `device_online` от
   1000006 после anchor. Событие 1000006 от 13:48 UTC возникло до первой
   оплаты и для начисления не используется. Владелец тестовой машины
   временно устанавливает 1000006 и запускает службу. По новому event ID
   сверить tenant, SN, terminal ID, `occurred_at > anchor_at`; затем штатно
   обработать это событие через finance metering. Один месячный charge
   должен списать 10000 коп.; повтор того же event ID обязан вернуть ту же
   запись без второго списания. Ожидаемый balance при отсутствии иных
   операций: −9000 коп. Агент вернуть на 1000007 и проверить online.

## Grace → blocked → stop (10–20 минут)

4. Только в изолированном backend включить policy для tenant 10000 и
   `L4DESK_ENTITLEMENT_TEST_TENANT_IDS=[10000]`. Рассчитать offset в момент
   старта как `grace_deadline − текущее UTC − 12 минут`; положительное
   целое значение не должно превышать 604800. Тестовый backend
   пересоздаётся с этим offset; production сохраняет реальное время.
5. Прочитать entitlement test05: `grace`, отрицательный баланс, прежние
   anchor/cycle/deadline. Проверить, что lease в изолированном backend
   разрешён в grace, и сразу освободить его. Открыть видео 1000007 на
   обычном сайте; записать ID активной сессии и убедиться, что кадры
   движутся. Выполнить адресный test tick: он должен показать `grace` и
   `sessions_stopped=0`.
6. Через 12–13 минут, не останавливая видео вручную, повторить адресный
   test tick. Ожидается `blocked`, один stop или уже закрытая сессия,
   `entitlement_blocked` в аудите, закрытие provider session и исчезновение
   живого видео. Если stop не дошёл, зафиксировать `stop_requested` и
   повторить только адресный tick; другие tenant не сканируются.
7. Новый lease 1000007 в изолированном backend должен дать
   `403 entitlement_blocked`; после отказа не должно появиться новой
   активной сессии. Проверить, что tenant 1000 и production commercial
   flags не изменились.

## Восстановление и доказательства

8. Через mock ЮKassa пополнить только tenant 10000 на сумму, покрывающую
   его проверенную задолженность; не использовать реальное списание.
   Повтор webhook/poll должен дать одну posted ledger transaction.
   Entitlement станет `active`, anchor/cycle останутся прежними;
   isolated lease разрешён и штатно освобождён.
9. Вернуть изолированному backend исходный allowlist квоты `[1000]`,
   policy=true, entitlement test allowlist `[]`, offset `0`; проверить
   текущие env-флаги внутри контейнера без вывода секретов. У tenant
   10000 не должно остаться активной сессии и отрицательного баланса.
   Месячный charge за реально подключённый 1000006 остаётся в ledger.
10. В отчёт внести timestamps, event/charge/ledger/session/stop IDs,
    балансы до и после, статусы HTTP, результаты повторов и отдельные
    наблюдения браузера. Уведомления в этом сценарии только ставятся в
    очередь; доставка email требует отдельной проверки.

## Стоп-условия

Не запускать адресный stop при неизвестном владельце `device_online`,
отсутствии нового события после anchor, неподтверждённом балансе,
неверном allowlist или активной сессии другого tenant в выбранном
провайдерском ID. При сбое сохранить факты, восстановить параметры
изолированного backend и закрыть тестовые аренды штатно.

## Подготовленное окружение и уровень доказательства

- Source commits `469fcd4` (tenant-scoped clock/tick) и `9003400`
  (остановка текущего stream через app1 lease, metering closure) запушены
  в release branch. Финальный Git archive SHA-256:
  `5543a13e8de37c2d54236e96efcd68ca7e414a0bee10ef46ef4b4fcfeccbb31d`.
- Только изолированный test-backend использует image
  `sha256:ce410ce51cc0c28c8c9c5c348c1a3b8189eaedf353b2b13f87d8eb3ed1d930e9`.
  Production MenuBuilder сохранил прежний image
  `sha256:e0d17a09092e34b206eeb313b155186c419e55ee0419a684eb5ae2ce32e50090`.
  Текущий test config: policy=true, quota tenant `[1000]`/600 с,
  entitlement clock allowlist `[]`, offset 0, IoT consumer=false.
  Test `/docs` ответил 200, неавторизованный scoped tick — 403,
  контейнер running без рестартов. Compose backup сохранён отдельно.
- Локально `uv run pytest -q --tb=line --disable-warnings`: 524 passed;
  `ruff check --fix app tests`, `ruff format app tests` и
  `pyright app tests/test_l4d_12_entitlement_grace_and_notifications.py`
  прошли. Широкий `pyright app tests` ранее показывал 20 существовавших
  ошибок в других тестах; изменённый код имеет 0 ошибок.
- Уровень на момент подготовки: локальные тесты и smoke тестового backend.
  Результат runtime-прогона приведён ниже.

## Результат runtime-прогона 2026-09-27

- Новый подтверждённый IoT `device_online` существующего 1000006:
  `evt_28f576a5b34c4ec8a1972452bdee37ca`, 15:54:39 UTC, tenant 10000,
  после anchor. 1000006 создан администратором, поэтому до первого обычного
  видеосеанса отсутствовал в `l4desk_terminals`: прямой metering API вернул
  500 из-за FK, вся транзакция откатилась. После штатного короткого видео
  запись появилась как ordinal 2; повтор начисления создал один charge 3,
  ledger transaction 8 на 10000 коп. Повтор event вернул тот же charge 3
  и transaction 8; баланс стал −9000 коп. Отдельный кодовый фикс `e244bae`
  возвращает 409 `terminal_not_enrolled` до проводки и развернут только в
  изолированном backend; smoke на неизвестном terminal 999999 дал 409.
- В test-backend разрешён только tenant 10000 для сдвига времени. Первый
  прогон: сеанс 493 начат в 16:10:28 UTC и прерван сетевым сбоем до
  deadline. Tick после границы вернул `blocked`, `sessions_stopped=1`, но
  provider lease уже отсутствовал; локальная запись получила `failed`,
  `entitlement_blocked`, а неподтверждённый хвост освобождён в пользу
  клиента. Новый lease получил 403 `entitlement_blocked`. Это **не**
  доказательство принудительного stop живого потока.
- Повторный сеанс 494 стартовал в 16:24:52 UTC, кадры двигались. Адресный
  tick до границы вернул `grace`, `sessions_stopped=0`; после границы —
  `blocked`, `sessions_stopped=1`. Текущий app1 stream остановлен;
  сеанс 494 закрыт в 16:28:49 UTC с причиной `entitlement_blocked`.
  Пользователь подтвердил, что видео в браузере остановилось. Новый lease
  test05 получил 403 `entitlement_blocked`; активных сессий tenant 10000
  после stop нет. В дневном агрегате бесплатного терминала 1000007 —
  879 source/video seconds и 0 posted kopecks, включая ранние тесты дня;
  агрегат сам по себе не является детализацией сеанса 494.
- Изолированный mock ЮKassa: платёж 5 на 10000 коп. переведён в
  `succeeded`; два poll указывают на одну ledger transaction 9. Баланс
  восстановлен до +1000 коп., entitlement=`active`. Проверка lease после
  восстановления: POST 201, DELETE 204.
- Итоговая конфигурация test-backend: image `e244bae` (из Git archive
  SHA-256 `ebc15c0c935cb3b24cc021147abafc06d4ebc853e3137b85dbdaab7a97d7d2c4`),
  policy=true, quota `[1000]`/600 с,
  entitlement test allowlist `[]`, offset 0, IoT consumer=false.
  Production image не менялся. Локально после `e244bae`: 524 теста,
  ruff и pyright `app` прошли. Широкий pyright тестового каталога всё ещё
  содержит 3 ранее известных ошибки в `test_tariffs_and_metering.py`.

Доставка email-уведомлений и реальный YooKassa не проверялись; здесь
подтверждены только постановка уведомлений и изолированный mock-платёж.
