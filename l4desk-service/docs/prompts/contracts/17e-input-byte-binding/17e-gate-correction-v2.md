# Исправление read grant для L4D-17E-MB-FIX-01

Статус: `VERIFIED` для документального controller gate. Это не новая
приёмка MenuBuilder и не runtime smoke.

Исходная регистрация `R-L4D-17E-MB-FIX-01-v1` разрешала читать
`nginx-configs/port_3000.conf` в commit
`f5017615a8a84eee318a78b54a992ceb45f91edc`. Принятый
`H-L4D-13-MB-v1` содержит SHA-256
`e76037bdee1d28411ff843d6ee8dc3e0e8dff3c95382e2e972566afda1f6ef0d`.
Git/raw SHA-256 в разрешённом commit был
`d99f2bb62ba67f796b2c104bafd81fb63a6e2b50fc355aaf8dbf9c12f6930399`;
LF → CRLF давал
`fdf0a4e2b22383421754d0155c8b6dbe7eb946089aedd42a966e22cc6f923165`.
Ни один не совпал с handoff; поэтому runtime-агент вернул
`BLOCKED_CONTRACT` в отчёте MenuBuilder commit `479e1b5`.

В самом принятом handoff есть `report_commit:
4a6e124d870e06a2001a83464af2beb0123639b5`. Сырые Git-байты
`nginx-configs/port_3000.conf` в этом commit имеют SHA-256
`e76037bdee1d28411ff843d6ee8dc3e0e8dff3c95382e2e972566afda1f6ef0d`:
точное совпадение без преобразования. Исправлению подлежит только
`artifact_commit` read grant для этого пути. Сам handoff и byte bindings
не меняются.

Перед подготовкой регистрации v2 контроллер заново сверил все 11 прямых
входов: 127/127 артефактов доступны в указанных Git-коммитах, из них
15/15 совпадают с принятым raw SHA-256 и 112/112 покрыты девятью
проверенными `LF_CRLF_ONLY` bindings. Оставшиеся условия 17E acceptance
исполнитель проверит после нового contract gate. Серверы, БД, тестовые
сессии и исходники MenuBuilder в этой controller-проверке не менялись.

Новая регистрация должна сохранить тот же `prompt_id` и список входов,
отозвать v1 по §8.5, указать этот `report_commit` как точный read grant,
а для immutable отчёта и candidate исполнителя задать новые пути.
