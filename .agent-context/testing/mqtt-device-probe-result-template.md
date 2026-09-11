# MQTT device-probe result

- Контур: local / test / explicitly approved remote.
- Согласование / isolation и отсутствие опасного bridge подтверждены:
- Эмулируемая identity: terminal SN `a4b0000773c82116d210826`.
- Terminal ID: `773`; binding у потребителей проверен:
- Режим: passive observe / contract publisher / lease observe / recovery observe.
- Client ID isolation и ACL направления подтверждены (без connection profile):
- Проверяемый flow / revisions producer и consumer:
- Producer:
- Consumer:
- Topic:
- Schema/version / допустимые поля:
- QoS / retain:
- Test correlation ID (в evidence, не новое wire поле):
- Wire correlation: command_id или correlationData; lease_id/stream_instance_id/session_id когда применимы.
- Окно наблюдения UTC / clock skew:
- Ожидаемый результат:
- Фактический результат / status passed, failed, not run:
- Полученный ACK/NACK или event / очищенный evidence:
- Реально проверенные слои / какие эмулировались:
- Destructive step: N/A или разрешение и ownership конкретного test PID:
- Cleanup выполнен: stop/release, disconnect, оставшиеся ресурсы / причина незавершения.
- Риски / отклонения / следующие действия:

Не включать секреты, пути к ключам, сертификаты, broker profile и приватный output.
Эмуляция terminal event не подтверждает реальные input/FFmpeg/recovery или весь E2E.