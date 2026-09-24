# L4D-08B-FIX-02-MB — Стабильное устранение конфликта session_id в media lifecycle и закрытие открытого риска тестов

```yaml
prompt_id: L4D-08B-FIX-02-MB
scope_project: MenuBuilder
scope_root: D:\repo\platerra\Public\etranprocessing\MenuBuilder
prompt_type: corrective-implementation-step
corrects_prompt_id: L4D-08B-FIX-01-MB
corrective_registration_required: true
sequence_gate_handoff_id: H-L4D-13-MB-FIX-01-v1
required_handoff_ids:
  - H-L4D-07-IOT-v1
  - H-L4D-08A-MEDIA-v1
  - H-L4D-08B-FIX-01-MB-v1
  - H-L4D-13-MB-FIX-01-v1
sequence_gate_status: BLOCKED_UNTIL_CORRECTIVE_REGISTERED
output_handoff_id: H-L4D-08B-FIX-02-MB-v1
next_prompt_id: L4D-14-MB
branch: l4desk/l4d-08b-fix-02-mb
report_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-02-MB-report.md
candidate_format: DETACHED_V1
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-02-MB-candidate.md
architecture_sections: [1, 3, 4, 5, 6, 7, 15, 16, 17]
consumers:
  - L4D-14-MB
  - L4D-16-MB
  - L4D-17E-MB
  - L4D-18E-MB
```

---

## 1. Цель и обязательный результат

Ты — агент-исполнитель corrective-шага **`L4D-08B-FIX-02-MB`** в единственном разрешённом проекте **`MenuBuilder`**.

Необходимо полностью и основательно устранить две выявленные проблемы после реализации задачи `L4D-08B-FIX-01-MB`:

1. **Проблема жизненного цикла сессий в `menubuilder.video` («No such mountpoint/stream 70 / 773»)**:
   - В коммите `0ba7641` идентификатор сессии `media_session_id` был зафиксирован как статический `f"media-{terminal.sn}"`.
   - В `l4media-ingress` (`media_lifecycle.h`) сессия после первой остановки переходит в статус `MEDIA_STATE_STOPPED`, а динамический mountpoint в Janus удаляется.
   - Повторные попытки старта трансляции с тем же статическим `media_session_id` отвергались сервисом Ingress со статусом `409 Conflict: session_terminated`.
   - В `video_control.py` и `remote_session_use_case.py` эта ошибка ошибочно воспринималась как признак «сессия уже активна» и подавлялась, в результате чего маунтпоинт в Janus не пересоздавался, а браузер получал ошибку «No such mountpoint/stream».
   - Решение: динамическое формирование и координация уникальных `session_id` на уровне сеанса, поддержка безопасной переаллокации при получении конфликтов `session_terminated`/`session_busy`, актуализация `provider_session_id` в локальном репозитории и корректный останов всех ассоциированных сессий при завершении трансляции и освобождении аренды.

2. **Падение теста `test_step6_quick_actions::test_stream_start_504_terminal_timeout_not_swallowed`**:
   - Вызов `media_orchestrator_client.start_session` перед стартом потока терминала вызывал сетевой `502 Bad Gateway` в изолированном тестовом окружении из-за отсутствия мока медиаоркестратора.
   - Решение: добавление мока `media_orchestrator_client.start_session` в тесте с подтверждением прохождения всех проверок и сохранения инварианта непроглатывания 504 ошибки от терминала.

---

## 2. Непереговорные инварианты

1. Единый путь оркестрации: MenuBuilder → IoT session lock → media lifecycle API → l4media-ingress (route + Janus mountpoint) → запуск terminal stream.
2. Никаких прямых обращений к Nginx ingress или Janus WebRTC минуя `l4media-ingress` (`H-L4D-08A-MEDIA-v1`).
3. Для каждого нового цикла трансляции гарантируется создание рабочего маунтпоинта в Janus Gateway.
4. Ошибки `session_terminated` никогда не маскируются под успешный запуск.
5. При остановке стрима или освобождении аренды корректно вызывается остановка медиасессии в orchestrator.
6. Все тесты в `MenuBuilder/backend` должны проходить (100% green).

---

## 3. Разрешённый scope изменений

- `MenuBuilder/backend/app/services/remote_session_use_case.py`
- `MenuBuilder/backend/app/routers/video_control.py`
- `MenuBuilder/backend/tests/test_step6_quick_actions.py`
- `MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-02-MB-report.md`
- `MenuBuilder/docs/l4desk/handoffs/L4D-08B-FIX-02-MB-candidate.md`
- `l4desk-service/docs/prompts/contract-handoff.md` (регистрация corrective шага)
