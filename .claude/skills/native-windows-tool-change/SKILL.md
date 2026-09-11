---
name: native-windows-tool-change
description: Изменения разрешённой tools-утилиты на C/C++, WinAPI/CNG/MQTT с проверкой ownership, cleanup, контрактов и штатной сборкой x86/x64/default.
---

# Native Windows tool change

1. Убедись, что tools и конкретная утилита разрешены задачей; не сканируй весь suite.
2. Определи launch flow, владельца дочернего процесса, cleanup, потоки/синхронизацию,
   logging и IPC/MQTT. Не трогай чужие процессы или пользовательские артефакты.
3. Перед созданием/изменением MQTT-клиента спроси:
   **«Какой тип MQTT-клиента создаётся: main_app или extra_service?»**
   При расхождении с существующим svc_desk уточни отдельное согласование;
   не выбирай тип автоматически и не переноси его presence на svc.
4. Для протокола проверь parse → validate → action → ACK/NACK → dedup → event.
   Используй [MQTT matrix](../../../.agent-context/contracts/mqtt-topic-matrix.md).
5. Сохраняй zero-dependency Win32/C и `/MT`, строй штатным build.cmd утилиты.
   В PowerShell перейди в её каталог и выполни `cmd /c build.cmd all`.
6. Проверь свежие x86, x64 и default artifact; не считай наличие старого exe сборкой.
   Packaging/installer/distribution не менять без прямой задачи; конфликт scope — уточнить.

## Checklist l4desk
- [ ] Подписка `srv/{SN}/ctl`, dedup по wire `command_id`.
- [ ] Renew проверяет lease_id/state/expiry; NACK вместо silent ignore при отказе.
- [ ] Renew реально сдвигает expiry, watchdog читает обновлённое значение.
- [ ] Reconcile публикует stopped/agent_restart_reconcile, только свои orphan processes.
- [ ] lease_expired не запускает recovery; unexpected_exit — bounded recovery с valid lease.
- [ ] Проверены `bin\x86`, `bin\x64`, `bin` и применимые runtime-сценарии.

## Результат
Diff scope, [l4desk context](../../../.agent-context/components/l4desk.md),
контрактная матрица, build artifacts/exit codes и непроверенные сценарии в handoff.