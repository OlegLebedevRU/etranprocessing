# Передача L4D-17E-MB-FIX-01

## Контекст задачи

- Цель / scope: подготовить формально допустимую повторную приёмку 17E;
  изменение только документации контроллера `l4desk-service`.
- Владелец runtime и коммерческих данных: `MenuBuilder`; медиатракт:
  `l4media`; handoff-регистрация: контроллер каскада.
- Producer → transport → consumer: десять принятых MenuBuilder handoff и
  новый media export → `contract-handoff.md` + immutable Git artifacts →
  `L4D-17E-MB-FIX-01`.
- Рабочая ветка: `release/l4tools-1.8.2-beta-1`; опубликованный пакет:
  `ccab98730d6fde1f2e5335c35bad63e3303f4227`.
- Scope исполнителя: `D:\repo\platerra\Public\etranprocessing-l4tools-182\MenuBuilder`.
- Инварианты: старые `ACCEPTED` блоки и отчёт 17E R неизменны;
  production commercial flags выключены; никаких реальных платежей;
  один scope на prompt и append-only acceptance контроллером.

## Выполнено

- Новый media subject handoff `H-L4D-17D-MEDIA-CONTRACT-01-v1` принят в
  `31122c3`; старый 17D — только sequence gate.
- Отчёт побайтной сверки опубликован в `c3cd784`; девять точных
  `ARTIFACT_BYTE_BINDING` и регистрация 17E FIX — в `ccab987`.
- Новый [prompt](../../../l4desk-service/docs/prompts/L4D-17E-MB-FIX-01.md)
  и его [регистрация](../../../l4desk-service/docs/prompts/contract-handoff.md)
  задают 11 прямых входов, два внешних Markdown и один декларативный
  nginx-конфиг для read-only проверки, девять
  binding ID, `DETACHED_V1` и новые пути отчёта/candidate.
- PR #4 оставлен draft; 17E и 17F не объявлены принятыми.

## Проверено

- [x] Публикация `ccab987` подтверждена `git ls-remote origin`.
- [x] У 11 входов ровно по одному блоку `ACCEPTED`; tuple
  `handoff_id`/`contract_version`/`producer_commit` совпал с регистрацией.
- [x] Промпт и регистрация совпали по scope, входам, bindings, read grants,
  output/next и путям; `detached_candidate_approved: true`.
- [x] Для 112 исторических SHA-256 совпала точная операция LF → CRLF;
  Git/raw SHA-256 и old SHA сверены для каждого binding artifact.
- [x] Media export: 2/2 опубликованных файла совпали с SHA-256 handoff.
- [x] У прямых входов не найдено `TBD/TODO/UNKNOWN`, отзыва или дубликата.
- [x] `git diff --check`; новые строки просмотрены на секреты.
- [ ] MenuBuilder suite и browser/runtime E2E: это работа 17E FIX,
  runtime и код в подготовке передачи не изменялись.
- [ ] Независимая приёмка нового report/candidate: они ещё не созданы.

## Начало исполнения

Передать исполнителю ровно опубликованный
`l4desk-service/docs/prompts/L4D-17E-MB-FIX-01.md` из `ccab987` и
подтверждение push. Вначале выполнить contract gate по стандарту; затем
проверки 17E только внутри MenuBuilder. Исторический отчёт
`L4D-17E-MB-acceptance-2026-09-27.md` использовать как evidence без правок.

После нового отчёта R2 и отдельного candidate C2 контроллер сверяет
runtime image/flags и digest независимо. Исходный prompt 17F ссылается
на старые 17D/17E ID: перед его запуском нужен отдельный корректирующий
адресный шаг. Решение о его точной форме зависит от принятого 17E FIX.

Cleanup: временные локальные скрипты сверки удалены; тестовые сессии,
серверные файлы и процессы в этой задаче не создавались и не менялись.
