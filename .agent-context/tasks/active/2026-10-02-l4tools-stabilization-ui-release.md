# L4 Tools: стабилизация, UI и выпуск

## Task intake
- Цель: поднять финальные tools, сохранить l4pin GUI/безопасную замену сертификата,
  стабилизировать сетевые/служебные пути, переработать l4setup UI, закрепить подпись.
- Scope: весь tools; текущая стадия — исследование и документирование плана.
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
