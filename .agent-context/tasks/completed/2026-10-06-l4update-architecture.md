# L4Update architecture — handoff

## Контекст задачи

- Scope завершённого шага: фиксация согласованной архитектуры и плана, без реализации.
- Владелец: tools/release; IoT — минимальная интеграция RPC/event, MB/PB без изменений.
- Исходный HEAD etranprocessing: a39a6521e4ef5935fbb93e32af2e82141889a750;
  результат — working tree без commit, 2026-10-06.
- Доступ: tools разрешены пользователем, внешний IoT read-only явно запрошен.
  Не исследовались исключённые legacy/SQL каталоги; broker/deploy не запускались.

## Выполнено

- [Авторитетный документ](../../../docs/term_arch-l4update-flow.md): принятые
  решения, границы evidence, минимальные RPC/event контракты, gates и этапы.
- Индексы docs/context, console/MQTT/validation контексты связаны с новым планом;
  требования отделены от текущей реализации.
- sw_sign.env добавлен в .gitignore; значения не читались/не сохранялись.

## Затронутые контракты

| Контракт | Producer | Consumer | Статус |
|---|---|---|---|
| RPC703x | IoT API/task | L4Con → worker | Accepted design, registration/implementation open |
| EVT/EVA | L4Con | IoT collector | Existing transport; new code/tag open |
| Windows paths | setup/release | Все tools/local consumers | Target layout, не внедрено |
| Release admission | uv pipeline/Registry | updater | Accepted design, не реализовано |

## Изменённые инварианты

- Новая раскладка вместо C:\l4tools без миграции; внешние backend-контракты сохраняются.
- Один встроенный ключ вместо сложной trust-root ротации; owner repair отдельно.
- Обязательная обратная совместимость связи без waiver; helper owner-only.

## Проверено

- [x] Read-only аудит IoT HEAD2ffa1403: completed REQ→NOP/zero UUID; EVT/EVA,
  polling whitelist и возможные внешние причины отказа барьера.
- [ ] Реализация release module/update/helper/layout/IoT registration — следующие шаги.
- [ ] Native build/tests, API/MQTT/live773, release signing/publication — не запускались.
- [ ] Setup второй стенд — ещё не назначен.
- [x] Относительные ссылки трёх новых Markdown-файлов разрешаются; naming и
  регистрация в docs/context/handoff индексах проверены.
- [x] git diff --check завершился без ошибок; git check-ignore подтвердил sw_sign.env.
- Backend/frontend quality checks N/A: код PB/MB/shared не изменён.

## Evidence и следующий шаг

- Уровень: документ и статические исходники, без runtime нового flow.
- [Карточка](../../components/l4update.md) содержит границы и open points.
- Начать этап1: audit существующего build/sign/publish и реализация единого uv
  конвейера. До изменения MQTT-клиента выполнить обязательное уточнение типа.
- Cleanup: ключи, процессы, артефакты и удалённые ресурсы не создавались.
- Секреты/connection profiles в handoff отсутствуют.
