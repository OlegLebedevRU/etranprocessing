# L4D-17F-DOCS — инвентаризация перед black-box E2E

```yaml
prompt_id: L4D-17F-DOCS
scope_project: l4desk-service
output_handoff_id: H-L4D-17F-DOCS-v1
status: BLOCKED_CONTRACT
deployment_status: NOT_CHANGED
candidate_status: NOT_CREATED
next_prompt_id: L4D-18A-SHARED
```

## Task intake и граница проверки

Цель — сохранить точную матрицу входов и недостающего evidence перед 17F. Владелец итогового black-box verdict — `l4desk-service`; producer-контракты остаются у соответствующих проектов. Инвентаризация использует опубликованные handoff/отчёты и результаты read-only проверки, не меняет runtime, БД или флаги. `ACCEPTED` возможен только после полного сценария `L4D-17F-DOCS.md` и независимой сверки всех прямых входов.

## Прямые входы 17F

| Требуемый handoff | Журнал | Текущая версия/evidence | Для 17F |
|---|---|---|---|
| `H-L4D-17A-TOOLS-v1` | §44 `ACCEPTED` | Agent artifact `1.7.7`, digest и backward compatibility записаны в §44; в этой итерации не скачивался и не запускался. | Перепроверить опубликованный artifact/version на целевом терминале. |
| `H-L4D-17B-PB-v1` | §45 `ACCEPTED` | Alembic `027`, certificate/PIN local tests и error-path smoke в принятом отчёте; `processing-backend` сейчас работает. | Проверить production image/commit и provisioning/PIN positive E2E только по безопасному тестовому пути. |
| `H-L4D-17C-IOT-v1` | §46 `ACCEPTED` | После принятого `7c6f75f` app1 watch handler обновлён в `d7b604a`; SHA-256 активного файла `/app/api/internal_v1/remote_input.py` и source — `f83a3194d882689df49264a2c90c50a05f2a2436e76623defa5777ffcd94ae4b`. Контейнер `app1` работает с одним worker по последнему provider report. | Связать post-acceptance commit с окончательной версией/контрактом, проверить image digest и event/usage path. Старый §46 не исправлять задним числом. |
| `H-L4D-17D-MEDIA-v1` | §47 `ACCEPTED` | Отчёт содержит start/healthy/stop, archive checks и zero orphan; ingress и Janus работают. | Проверить deployed media version и путь свежего кадра в black-box E2E. |
| `H-L4D-17E-MB-v1` | **отсутствует** | [17E report](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-report.md) имеет `BLOCKED_CONTRACT`; обязательный `H-L4D-17C-VIDEO-WATCH-MB-v1` также отсутствует. MenuBuilder backend/frontend local checks прошли, live version файлов watch совпала с репозиторием; коммерческие флаги выключены. | Принять corrective watch, затем полный 17E commercial gate. Без этого 17F sequence gate не пройден. |

Дополнительный watch provider `H-L4D-17C-VIDEO-WATCH-IOT-01-v1` принят в §43; текущий consumer `H-L4D-17C-VIDEO-WATCH-MB-v1` лишь зарегистрирован (§48). [Контрольная точка](../prompts/L4D-17E-PRELAUNCH-CHECKPOINT.md) описывает post-acceptance изменения и риск следующего выпуска MenuBuilder из старого `main`.

## Недостающее для безопасного 17E/17F E2E

Пользователь подтвердил, что утверждённой тестовой процедуры пока нет. Нужны без передачи секретов в чат или отчёты:

1. Выделенный test tenant и старый test user с подтверждённым ownership; процесс выдачи краткоживущих credentials и прав, включая негативный tenant/permission case.
2. Тестовый terminal/Agent с известной версией и разрешением на provisioning/PIN, online/offline, console/video; правило возврата устройства в исходное состояние.
3. Безопасная email delivery для self-registration и подтверждения адреса, с отдельным получателем/sink и cleanup.
4. YooKassa mock/sandbox или иной утверждённый платёжный тестовый маршрут без реальных списаний/чеков; сценарии webhook, poll, duplicate, manual payment/storno и способ уборки тестовых записей.
5. Изолированный financial baseline и правила сверки ledger/balance/usage, first payment anchor, 120-minute free limit, month/DST/grace/block без изменения данных реальных клиентов.
6. Разрешённый архивный sample/manifest и порядок restore/retention проверки без purge реальных данных.
7. Correlation IDs и окно наблюдения для всей цепочки, критерии свежего WebRTC кадра и отсутствия одновременных console/video, подтверждение версий images/flags/rollback и cleanup всех тестовых ресурсов.

## Статус сценариев 17F

Registration → email → tenant → terminal → provisioning/PIN → current Agent online → console/video → session facts → usage → payment → ledger/balance → grace/block/stop → Hub → archive: **NOT RUN**. Старый user, duplicate/retry, correlation и взаимное исключение: **NOT RUN**. Принятые отчёты и локальные suite подтверждают отдельные слои, но не эту сквозную цепочку.

`H-L4D-17F-DOCS-v1` не добавлять в `contract-handoff.md`. После принятия 17E и подготовки тестовой процедуры обновить inventory по фактическим deployed versions, затем выполнить полный black-box E2E и выпустить новый отчёт с реальным verdict.
