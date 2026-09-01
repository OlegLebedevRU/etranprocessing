---
name: rabbitmq-diagnostics
description: Generates a comprehensive analytical report on RabbitMQ broker status (queues, topics/bindings, user breakdown, connection stats, message throughput rates, memory/disk consumption, and error log diagnostics) on the production server. Use when the user asks to inspect RabbitMQ, check queues, analyze message rates, diagnose MQTT/AMQP connections or investigate RabbitMQ errors and logs.
---

# RabbitMQ Diagnostics & Reporting Skill

Guide and automated operational workflow for inspecting, monitoring, and generating full analytical reports on the RabbitMQ messaging broker deployed on the primary application server (`176.108.247.249`).

---

## 1. Environment & Server Reference

- **Host**: `176.108.247.249` (user `user1`, SSH key `d:\.ssh\free-tier-cloud_ru`)
- **Container**: `rabbitmq` (managed via Docker Compose in `/home/user1/server/` or standard Docker)
- **Broker Version**: `RabbitMQ 4.1.4` (Erlang/OTP 27)
- **Port Mapping & Protocols**:
  - `5672` — AMQP 0-9-1 (Backend services: `processing-backend`, `menubuilder-backend`, `app1`)
  - `1883` — MQTT Plain TCP
  - `8883` — MQTT over TLS (mTLS client certificate termination)
  - `15672` — Management HTTP API (`http://127.0.0.1:15672/rabbitmq/api/`)
  - `25672` — Clustering / CLI distribution

---

## 2. Pre-Flight Verification & Host Health

Before running deep diagnostics, verify SSH connectivity and host resource safety:

```powershell
# 1. Verify container is running
ssh -n -o StrictHostKeyChecking=no -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker ps --filter name=rabbitmq --format 'table {{.ID}}\t{{.Names}}\t{{.Status}}\t{{.Ports}}'"

# 2. Check host RAM and Disk
ssh -n -o StrictHostKeyChecking=no -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "free -h; df -h /"
```

Safety criteria: Available RAM > 300 MiB (server has 0B Swap), Root Disk `/` usage < 90%.

---

## 3. Telemetry & Data Collection Workflows

### 3.1. Node Status, Uptime & Active Plugins
```powershell
ssh -n -o StrictHostKeyChecking=no -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker exec rabbitmq rabbitmqctl status; echo '=== ENABLED PLUGINS ==='; sudo docker exec rabbitmq rabbitmq-plugins list -e"
```

### 3.2. Users, Tags & ACL Permissions Breakdown
```powershell
ssh -n -o StrictHostKeyChecking=no -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker exec rabbitmq rabbitmqctl list_users; echo '=== PERMISSIONS ==='; sudo docker exec rabbitmq rabbitmqctl list_permissions"
```
*Note: Categorize users into Administrator (`user`), Service accounts (`etran_service`), and Devices/Terminals (e.g. `[device]` tag).*

### 3.3. Queues, Consumers & Message Backlog
```powershell
ssh -n -o StrictHostKeyChecking=no -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker exec rabbitmq rabbitmqctl list_queues name messages consumers type memory state"
```
*Categorize queues into Core/System queues (`req`, `res`, `app`, `svc`, `ack`, `evt`, `out`, `iot.device.connection.events`, background jobs) and MQTT subscription queues (`mqtt-subscription-*`).*

### 3.4. Exchanges, Topics & Bindings
```powershell
ssh -n -o StrictHostKeyChecking=no -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker exec rabbitmq rabbitmqctl list_exchanges name type; echo '=== BINDINGS ==='; sudo docker exec rabbitmq rabbitmqctl list_bindings source_name source_kind destination_name destination_kind routing_key"
```

### 3.5. Connections, Channels & Message Throughput (Overview)
Fetch message rates, publish/deliver totals, and active connections via Management API or rabbitmqctl:
```powershell
# Query Management Overview API via host bridge:
ssh -n -o StrictHostKeyChecking=no -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "curl -s -u user:root http://172.22.0.3:15672/rabbitmq/api/overview | jq '{message_stats, object_totals, queue_totals}'"

# Or query connection counts:
ssh -n -o StrictHostKeyChecking=no -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker exec rabbitmq rabbitmqctl list_connections name protocol user peer_host peer_port channels"
```

### 3.6. Memory Breakdown & Disk Space Usage
```powershell
ssh -n -o StrictHostKeyChecking=no -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker stats --no-stream rabbitmq; echo '=== MEMORY BREAKDOWN ==='; sudo docker exec rabbitmq rabbitmq-diagnostics memory_breakdown; echo '=== DISK INFO ==='; sudo docker exec rabbitmq df -h /var/lib/rabbitmq"
```

### 3.7. Log Analysis & Error Diagnostics
Inspect recent logs for errors, connection drops, auth refusals, and TLS handshake issues:
```powershell
ssh -n -o StrictHostKeyChecking=no -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker logs --tail 1000 rabbitmq 2>&1 | grep -o '\[error\].*' | cut -d' ' -f3- | sort | uniq -c | sort -nr | head -n 25"
```

---

## 4. Report Synthesis Template

When asked to generate the RabbitMQ report, produce output formatted in valid Markdown with level 3 headings (`###`) following this structure:

```markdown
### Отчет о состоянии RabbitMQ

### 1. Общие сведения и статус ноды
- **Версия RabbitMQ / Erlang**: <RabbitMQ Version> / <Erlang Version>
- **Uptime**: <uptime in hours/days>
- **Активные плагины**: <list of active plugins>
- **Слушатели портов**: <5672, 1883, 8883, 15672, 25672>

### 2. Пользователи и права доступа
- **Всего пользователей**: <Total users count>
- **Администраторы**: <count & names>
- **Сервисные учетные записи**: <count & names>
- **Устройства / Терминалы**: <count & status of permissions/ACL>

### 3. Очереди (Queues)
- **Всего очередей**: <Total queues count> (сообщений в очереди: <count>, unack: <count>, активных консьюмеров: <count>)
- **Сервисные очереди**: <req, res, app, svc, ack, evt, out, connection.events, billing/jobs>
- **Очереди MQTT-подписок**: <service subscriptions and device QoS queues>

### 4. Топики, Exchanges и маршрутизация
- **Обменники**: <list of exchanges>
- **Паттерны топиков**:
  - `dev.*.<type>` -> <service queues>
  - `srv.<SN>.<type>` -> <terminal command/response routing>
  - `dev.*.gauge.state` -> <monitoring subscriptions>
  - `connection.created` / `connection.closed` -> <connection events>

### 5. Соединения и интенсивность обмена
- **Активные соединения**: <total count> (AMQP: <count>, MQTT/TLS: <count>)
- **Всего обработано сообщений**:
  - Опубликовано / подтверждено: <count>
  - Доставлено / подтверждено: <count>
  - Записей на диск: <count>
- **Текущая интенсивность**:
  - Публикация: <rate msg/s>
  - Доставка / ACK: <rate msg/s>
  - Задержки (Lag): <count>

### 6. Потребление памяти и дискового пространства
- **Оперативная память (RAM)**:
  - Контейнер Docker: <MiB> из <GiB> (<%>).
  - Процесс Erlang RSS: <MB>.
  - Порог High Watermark: <GB>.
  - Распределение памяти: <processes %, code %, system %, queues %>
- **Дисковое пространство**:
  - Файловая система: <used> / <total> (<%>, свободно <GB>).
  - Порог Disk Low Watermark: <MB>.
  - I/O контейнера: <read> чтение, <write> запись.

### 7. Анализ логов
- **Общее состояние**: <стабильность, отсутствие падений>
- **Выявленные ошибки и предупреждения**:
  - Ошибки прав доступа (ACL / blocked devices)
  - Ошибки авторизации (несуществующие пользователи)
  - Ошибки TLS-хэндшейка (устаревшие наборы шифров у клиентов)

### 8. Резюме и рекомендации
- <краткие выводы и рекомендуемые действия>
```
