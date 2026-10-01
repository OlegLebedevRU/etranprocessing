# L4 Tools: стабилизация, UI и выпуск

## Task intake
- Цель: поднять финальные tools, сохранить l4pin GUI/безопасную замену сертификата,
  стабилизировать сетевые/служебные пути, переработать l4setup UI, закрепить подпись.
- Scope: весь tools; код и unsigned build выполнены, ожидается controlled runtime и подпись.
- Владелец: native-инструменты; упаковка l4setup + tools/release. Серверные API без изменений.
- Flow: final source → x86/x64 build → signed stage → embedded payload → signed setup → registry.
- Инварианты: сертификат сохраняется при отказе; shutdown ограничен и безопасен;
  policy MQTT/RTP не рушит локальный ready; MQTT-типы/контракты сохраняются;
  секреты вне Git; обязательные Authenticode + RFC3161 timestamp.
- Источник рисков: `D:\.codex\check_one_fail_case_l4tools_windows-10-pro.md`.
  Не считать все его гипотезы доказанными на текущем runtime.
- Подтверждено кодом: l4pin DEFAULT_PROXY без enrollment timeouts;
  l4superv stop→kill без ожидания; l4setup фиксированные px-шрифты.
- Расхождение: main84bf3aa не содержит l4pin gui.c/gui.h и финальный сертификатный
  набор из fix/l4pin-cert-cleanup (a4556c8, handoff d4260af). Нужна адресная консолидация.
- l4con1.9.4 уже в main77b41a0 и ранее проверен на773; пакет ещё1.9.3.
- Подпись уже существует: Complete-SignedRelease.ps1 → Sign-Executables.ps1;
  не изобретать новый путь. Оператор подписывает до продолжения публикации.
- UI: две панели, постоянный читаемый журнал, сводка проблем, основное действие
  Install/Upgrade справа, вторичное Cancel; Retry только в применимом состоянии;
  адаптивный размер/DPI, цвет+текст, привычная иерархия действий.
- План/проверки: [полный план](../../../docs/term_dev-l4tools-stabilization-ui-release-plan.md).
- Не выполнены сейчас: code changes, builds, runtime, service operations, выпуск/push.
- Следующий шаг: сверить финальный набор по каждому инструменту, начать с восстановления
  l4pin и подтверждённых проблем HTTP, затем superv/proxy/con/mosquitto/setup и release.

## Реализация разрешена 2026-10-02
Ветка feat/l4tools-stabilization-cascade. [Единый отчёт](../../../docs/term_dev-l4tools-cascade-report.md). Этап00 завершён; этап01 l4pin выполняется. Неподтверждённые runtime/подпись отмечаются отдельно.

## Текущий handoff

- Ветка feat/l4tools-stabilization-cascade от main84bf3aa; implementation checkout
  D:\work\etranprocessing-mcp-user-events. Пользовательский dirty root/native files не заменяются.
- Отчёт этапов00–07: docs/term_dev-l4tools-cascade-report.md; read-only preview PNG приложен.
- Решения пользователя: normal native HTTP NO_PROXY; legacy certsrv не запускает MQTT/RTP;
  после verified new certificate cleanup legacy/old iot in Machine MY и всех profile MY.
- Собраны package1.9.4, pin1.7.3, proxy1.7.3, con1.9.5, superv/install1.9.3, desk1.9.3;
  l4capture актуального main1.0.0 и sql1.7.6. Staged18 EXE; unsigned, registry не обновлялся.
- Tests: pin certificate8/8, bounded HTTP/JSON/profile registry fixtures; superv mock SCM/process/identity;
  con MQTT5, user-event IPC/Job/rate и discovery; proxy policy/selection/lifetime + real loopback TLS
  with retired credentials; setup isolated8/8. Архитектуры x86/x64. Backend/frontend tests не запускались.
- Не запускать старый superv test_wait_active_component: использует live SCM names.
  Setup Machine ROOT test теперь только явный --allow-machine-ca; default isolated tests безопасны.
- OpenH264 vendor не tracked; восстановлены API headers и6 libs из локального capture-debug cache
  с совпадающими SHA256. Не копировались старые first-party EXE. Этот cache сохраняется для builds.
- Runtime MY/profile hive issuance/PIN, services under superv, media policy/log growth, actual installer
  upgrade/cancel и physical mixed-DPI transitions не проверены. Установленные EXE не заменялись.
- Automatic approval review отклонил составной live-cert diagnostic command (blocked by policy,
  без подробной причины). Не повторять его обходом; narrower read-only UI preview разрешён.
- Далее: конкретная operator-coordinated service stop для controlled runtime; backup перед заменой.
  Потом existing signing handoff (tools/release/README.md), verify signatures+timestamps+hashes,
  publish exact signed package. Не rebuild components после подписания.
