# L4C-04-RTP-SENDER — Передача H.264 по RTP/RTCP (RFC 3550, RFC 6184 FU-A), маркер AU, неблокирующий loopback UDP и исходящий RTCP SR/SDES

```yaml
prompt_id: L4C-04-RTP-SENDER
scope_project: tools/l4capture
scope_root: D:\repo\platerra\Public\etranprocessing\tools\l4capture
prompt_type: implementation-step
required_handoff_ids:
  - H-L4C-03-v1
sequence_gate_status: READY_FOR_L4C_04
output_handoff_id: H-L4C-04-v1
next_prompt_id: L4C-05-AGENT-ADAPTER
branch: l4capture/l4c-04-rtp-sender
report_path: docs/l4capture/handoffs/L4C-04-RTP-SENDER-report.md
candidate_format: DETACHED_V1
candidate_path: docs/l4capture/handoffs/L4C-04-RTP-SENDER-candidate.md
architecture_sections: [1, 2, 3, 5, 8, 9, 10, 12, 13, 14]
consumers:
  - L4C-05-AGENT-ADAPTER
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
```

---

## 1. Цель и архитектурная миссия

Ты выступаешь в роли **Ведущего сетевого инженера медиапротоколов и RTP/RTCP передачи (L4Capture-RTPSender-Agent)** в рамках комплекса `tools suite`.

Твоя задача — реализовать native-модуль сетевой передачи медиапотока H.264 по протоколам **RTP (RFC 3550, RFC 6184)** и исходящего **RTCP (SR + SDES CNAME, RFC 3550)** через локальный UDP loopback (`127.0.0.1:5004` / `127.0.0.1:5005`) к `leo4proxy`, а также замкнуть сквозной конвейер:
```text
GDI Capture → Bilinear Scale → BT.601 I420 → OpenH264 Encoder → RFC 6184 RTP Packetizer → Non-blocking UDP Loopback
```
в целевой директории `tools\l4capture\`.

Сетевой медиатранспорт обязан быть предельно утилитарным: максимизировать сквозную надёжность и пропускную способность, предотвращать накопление сетевых задержек (bufferbloat) и исключать каскадные сбои на тысячах необслуживаемых платёжных терминалов с разнородными сетевыми каналами (Ethernet, 3G/LTE, VPN).

На шаге **L4C-04-RTP-SENDER** создаются и интегрируются ключевые компоненты согласно архитектурным требованиям `l4capture_arch_final.md` (§5, §10, §12, §13, §14):
1. **RTP-пакетизатор H.264 (RFC 6184, `packetization-mode=1`):**
   - **Single NAL Unit Packet (§5.6):** Если размер полезной нагрузки NAL $L \le 1200$ байт — пакет содержит 12-байтный заголовок RTP и NAL целиком.
   - **Fragmentation Unit (FU-A, §5.8):** Если размер NAL $L > 1200$ байт — NAL фрагментируется. Бюджет RTP payload строго $\le 1200$ байт, включая 2 байта заголовков FU-A (FU indicator + FU header). Исходный заголовок NAL вырезается, а фрагменты несут максимум **1198 байт** чистых данных.
   - **Инвариант Access Unit Timestamp:** Все RTP-пакеты (NAL или FU-A фрагменты), принадлежащие одному и тому же Access Unit (кадру), обязаны иметь **строго одинаковый RTP timestamp** (`timestamp = base_timestamp + elapsed_pts_ms * 90`).
   - **Инвариант Marker Bit:** Бит `M` (Marker) устанавливается в `1` **исключительно у последнего RTP-пакета Access Unit** (последнего фрагмента последнего NAL кадра). У всех предшествующих пакетов кадра `M = 0`.
2. **Параметры сессии RTP (RFC 3550):**
   - Динамический Payload Type: `96` (`PT=96`).
   - Clock rate: `90 000` Гц (90 kHz, стандарт H.264 video).
   - Системный криптографический генератор случайных чисел (`CryptGenRandom` / `rand_s`) для начальных значений SSRC (32 бита), sequence number (16 бит) и base timestamp (32 бита).
   - Монотонный инкремент sequence number на каждый отправленный RTP-пакет modulo $2^{16} = 65536$.
3. **Неблокирующий сокет UDP Winsock (`127.0.0.1:5004`):**
   - Назначение строго `127.0.0.1`, порт `5004` по умолчанию (настраивается через CLI / IPC `CMD_START`).
   - Неблокирующий режим сокета (`ioctlsocket(FIONBIO)`).
   - Фиксация размера буфера сокета `SO_SNDBUF` (256 КиБ).
   - Защита от ICMP Port Unreachable: отключение `SIO_UDP_CONNRESET` через `WSAIoctl`, предотвращающее ошибку `WSAECONNRESET` при старте до запуска прокси.
   - **Политика сетевой перегрузки (Fail-Fast Drop Policy):** При невозможности немедленной отправки (`WSAEWOULDBLOCK` / `SOCKET_ERROR`) остаток пакетов текущего Access Unit немедленно **отбрасывается** (`drop AU`), счётчик `transport_drops` увеличивается, и немедленно инициируется вызов `force_idr()` на энкодере. Накопление очередей и передача P-кадров с потерянной базой **категорически запрещены**.
4. **Исходящий составной RTCP (RFC 3550 Compound Packet):**
   - Периодическая генерация раз в 1.0 секунду (1000 мс) монотонного времени на порт `5005`.
   - **Sender Report (SR, PT=200):** NTP Timestamp (64 бита, смещение от 1900 г. 2208988800), соответствующий RTP timestamp, cumulative packet count, cumulative octet count (чистый полезный объём RTP payload без 12-байтных заголовков).
   - **Source Description (SDES, PT=201):** SSRC, chunk CNAME (строка идентификатора, например `"l4capture@127.0.0.1"`), завершающий END (0) и выравнивание по 32 бита.
   - **RTCP BYE (PT=203):** Best-effort отправка пакета BYE при штатном завершении без задержки shutdown процесса.
5. **Защитный парсер обратного RTCP (Stub/Guard):**
   - Обратные RR/PLI не требуются для v1/M-1. Тем не менее сокет RTCP снабжается безопасным обработчиком: парсинг заголовков, проверка длины и SSRC, распознавание Payload-Specific Feedback (PT=206, FMT=1 = PLI) с ограничением частоты (rate limit $\le 1$ раз в 500 мс, коалесцирование с `force_idr`).
6. **Сквозная интеграция конвейера и CLI:**
   - Замыкание сквозного цикла захвата, сжатия и отправки.
   - При успешной отправке первого кадра SPS + PPS + IDR генерируется `EVENT_READY` в супервизор.
   - Тестовый CLI запускается с теми же deadline/safety ограничениями, исключая возможность обхода защит.
7. **Zero-Allocation Invariant:**
   - Запрет динамических выделений памяти (`malloc`/`free`) в горячем цикле пакетизации и отправки RTP. Буферы пакетов (MTU buffer $\le 1500$ байт) предвыделяются при создании отправителя или размещаются на стеке.

---

## 2. Непереговорные рамочные принципы и изоляция

1. **Строгая изоляция директорий (Directory Boundary):**
   - **Код, заголовки, сборка и тесты:** размещаются строго в `tools\l4capture\`.
   - **Документация, промпты, журнал контрактов:** находятся в `docs\l4capture\`.
   - **Отчёты и candidate-файлы:** формируются строго в `docs\l4capture\handoffs\`.
   - **КАТЕГОРИЧЕСКИЙ ЗАПРЕТ `l4desk-service`:** Строжайше запрещено создавать, изменять или использовать файлы внутри каталога `l4desk-service`. Проект `l4capture` полностью автономен.
   - **ЗАПРЕТ на модификацию других подсистем:** Запрещено изменять файлы в `BACK\`, `FRONT\`, `tools\l4desk`, `ProcessingBackend\`, `MenuBuilder\`, `shared\`, `sqlFileExample\`, `stored-procedures\`.
2. **Стандарты чистого C и компиляции:**
   - Код пишется строго на **C99/C11** (подмножество MSVC, компиляция через `cl.exe`, не C++).
   - Статическая компоновка Runtime: обязательный флаг `/MT` для Release-конфигурации (никаких динамических зависимостей `msvcrt*.dll`, `vcruntime*.dll`).
   - Поддержка Windows 7 SP1 x86/x64: компиляция с `/D_WIN32_WINNT=0x0601`, компоновка x86 с `/SUBSYSTEM:CONSOLE,6.01`, x64 с `/SUBSYSTEM:CONSOLE`.
   - Таблица импортов (`dumpbin /dependents`): только базовые системные библиотеки (`KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ADVAPI32.dll`, `OLE32.dll`, `WS2_32.dll`).
   - Линковка Winsock: библиотека `Ws2_32.lib`.
   - Нулевая толерантность к предупреждениям компилятора: уровень `/W4` с zero warnings.
3. **Управление памятью (Zero-Allocation Invariant):**
   - Контекст RTP-отправителя, сокеты и структуры пакетизатора выделяются один раз при инициализации (`l4c_rtp_sender_create`).
   - В цикле обработки кадра и отправки пакетов (`l4c_rtp_send_au`) вызовы `malloc`, `free`, `realloc` **категорически запрещены**.
4. **Сетевой бюджет MTU:**
   - Бюджет полезной нагрузки RTP строго $\le 1200$ байт.
   - С учётом 12-байтного заголовка RTP размер UDP-пакета составляет $\le 1212$ байт, что гарантирует прохождение через любые туннели loopback/VPN без IP-фрагментации.

---

## 3. Pre-Flight Check & Contract Gate (Шаг 1)

Перед началом внесения изменений агент обязан выполнить валидацию входных контрактов:

1. Открой файл журнала `docs\l4capture\prompts\contract-handoff.md`.
2. Убедись, что блок `H-L4C-03-v1` присутствует в секции `## 5. Принятые handoff-блоки` и имеет статус `ACCEPTED`.
3. Сверь контрольные суммы входных артефактов из блока `H-L4C-03-v1`:
   - `docs/l4capture/handoffs/L4C-03-OPENH264-CODEC-report.md`: `b84a3b8f130bc1f3ec2953a1fb8d3c53d1d698bb841fcd496c9058d78d1abdb8`
   - `tools/l4capture/include/l4capture/openh264_encoder.h`: `8cec838fdb3883a5424829e7a0558679512c320c0b8c9d943fd81f326272e63e`
   - `tools/l4capture/bin/x86/l4capture.exe`: `490ae08441742bc0fb497f050df26145e1282f549584a47b37c505f32b59faca`
   - `tools/l4capture/bin/x64/l4capture.exe`: `8425d0401df9d0c482430e8b758c54e232d52f62701eb159545e57d703fdfda3`
4. Проверь рабочую ветку Git: `l4capture/l4c-04-rtp-sender`.
5. При обнаружении несоответствий или повреждений заверши работу со статусом `BLOCKED_CONTRACT`.

---

## 4. Архитектурные требования и техническая спецификация

Реализуй модуль сетевой передачи RTP/RTCP и пакетизации H.264 в строгом соответствии со спецификацией:

### 4.1. Конфигурация и интерфейс RTP Sender (`include/l4capture/rtp_sender.h`)

Создай заголовочный файл `include/l4capture/rtp_sender.h`:

```c
#ifndef L4C_RTP_SENDER_H
#define L4C_RTP_SENDER_H

#include "types.h"
#include "encoder_backend.h"

#define L4C_RTP_DEFAULT_HOST         "127.0.0.1"
#define L4C_RTP_DEFAULT_PORT         5004
#define L4C_RTCP_DEFAULT_PORT        5005
#define L4C_RTP_PAYLOAD_TYPE_H264    96
#define L4C_RTP_CLOCK_RATE_HZ        90000
#define L4C_RTP_MAX_PAYLOAD_SIZE     1200
#define L4C_RTP_MAX_FU_PAYLOAD_SIZE  1198
#define L4C_RTP_HEADER_SIZE          12
#define L4C_RTCP_SR_INTERVAL_MS      1000

typedef struct l4c_rtp_config {
    const char *dest_ip;         /* Назначение: "127.0.0.1" */
    uint16_t rtp_port;           /* Порт RTP: 5004 */
    uint16_t rtcp_port;          /* Порт RTCP: 5005 */
    uint8_t payload_type;        /* 96 */
    uint32_t ssrc;               /* SSRC (0 = сгенерировать крипто-RNG) */
    const char *cname;           /* CNAME для RTCP SDES, напр. "l4capture@127.0.0.1" */
} l4c_rtp_config_t;

typedef struct l4c_rtp_stats {
    uint32_t packets_sent;
    uint64_t bytes_sent;         /* Полезная нагрузка H.264 (без заголовков RTP) */
    uint32_t transport_drops;    /* Число отброшенных AU при насыщении сокета */
    uint32_t rtcp_sr_sent;
    uint32_t last_rtp_timestamp;
    uint64_t last_send_tick_ms;
} l4c_rtp_stats_t;

typedef struct l4c_rtp_sender l4c_rtp_sender_t;

/* Создание и инициализация сокетов UDP loopback */
l4c_status_t l4c_rtp_sender_create(const l4c_rtp_config_t *config, l4c_rtp_sender_t **out_sender);

/* Пакетизация Access Unit и отправка в UDP сокет */
l4c_status_t l4c_rtp_send_au(l4c_rtp_sender_t *sender, const l4c_access_unit_t *au);

/* Периодический опрос и отправка исходящего RTCP SR/SDES (вызывать в цикле конвейера) */
l4c_status_t l4c_rtp_sender_poll_rtcp(l4c_rtp_sender_t *sender, bool *out_pli_received);

/* Получение статистики передачи для телеметрии */
void l4c_rtp_sender_get_stats(const l4c_rtp_sender_t *sender, l4c_rtp_stats_t *out_stats);

/* Отправка RTCP BYE (best effort) и освобождение ресурсов/сокетов */
void l4c_rtp_sender_destroy(l4c_rtp_sender_t *sender);

#endif /* L4C_RTP_SENDER_H */
```

### 4.2. Спецификация заголовка RTP и пакетизатора RFC 6184

#### Формат заголовка RTP (12 байт, Network Byte Order / Big-Endian):
```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       sequence number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           synchronization source (SSRC) identifier            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```
- Байт 0: `0x80` (`V=2`, `P=0`, `X=0`, `CC=0`).
- Байт 1: `(marker ? 0x80 : 0x00) | (PT & 0x7F)`.
- Байты 2–3: `htons(seq_num++)`.
- Байты 4–7: `htonl(timestamp)`.
- Байты 8–11: `htonl(ssrc)`.

#### Алгоритм пакетизации NAL внутри Access Unit:
Для каждого NAL `k` от `0` до `au->nal_count - 1`:
1. Извлечь длину $L = \text{nal}[k]\text{.length}$ и указатель $D = \text{nal}[k]\text{.data}$.
2. Первый байт NAL является исходным заголовком: $\text{hdr} = D[0]$.
3. Проверить граничный признак последнего пакета AU: пакет является последним пакетом кадра только если он завершает последний NAL кадра ($k == \text{au->nal\_count} - 1$).

##### Вариант А: Single NAL Unit Packet ($L \le 1200$ байт)
- Пакет состоит из 12 байт RTP-заголовка + $L$ байт полезной нагрузки NAL (начиная с байта заголовка NAL).
- Маркер $M = 1$ выставляется, если $k == \text{au->nal\_count} - 1$, иначе $M = 0$.
- Пакет отправляется через сокет UDP.

##### Вариант Б: Fragmentation Unit FU-A ($L > 1200$ байт)
- Исходный 1-байтный заголовок NAL вырезается. Оставшаяся полезная нагрузка:
  $P = D + 1$, длина $L_{\text{payload}} = L - 1$.
- Максимальный размер данных на один фрагмент: $S_{\text{max}} = 1198$ байт.
- Общее число фрагментов: $N = (L_{\text{payload}} + S_{\text{max}} - 1) / S_{\text{max}}$.
- Формируется 1-байтный **FU Indicator**:
  $$\text{FU\_indicator} = (\text{hdr} \ \& \ \text{0xE0}) \ | \ 28$$
  (сохраняются биты `F` и `NRI`, тип выставляется в 28 = FU-A).
- Для каждого фрагмента $i$ от $0$ до $N - 1$:
  - Вычисляется длина фрагмента: $S_i = \min(1198, L_{\text{payload}} - \text{offset})$.
  - Формируется 1-байтный **FU Header**:
    - Start bit $S = (i == 0) \ ? \ 1 : 0$;
    - End bit $E = (i == N - 1) \ ? \ 1 : 0$;
    - Reserved bit $R = 0$;
    - Type = $\text{hdr} \ \& \ \text{0x1F}$ (исходный тип NAL);
    $$\text{FU\_header} = (S \ll 7) \ | \ (E \ll 6) \ | \ (\text{hdr} \ \& \ \text{0x1F})$$
  - Маркер $M = 1$ выставляется **ТОЛЬКО** если это последний фрагмент ($i == N - 1$) **И** последний NAL кадра ($k == \text{au->nal\_count} - 1$). Во всех остальных случаях $M = 0$.
  - Пакет собирается в предвыделенный буфер:
    - Байты 0..11: RTP Header (12 байт)
    - Байт 12: FU Indicator (1 байт)
    - Байт 13: FU Header (1 байт)
    - Байты 14..(14 + $S_i$ - 1): Срез полезной нагрузки $P[\text{offset} .. \text{offset} + S_i - 1]$
    - Полная длина пакета: $14 + S_i$ байт (не более $14 + 1198 = 1212$ байт).
  - Пакет отправляется через сокет UDP.
  - $\text{offset} += S_i$.

### 4.3. Таймстемпы и Access Unit семантика

1. При запуске сессии начальный RTP timestamp $T_0$ генерируется криптографическим RNG.
2. При приходе кадра `au`:
   $$T_{\text{frame}} = T_0 + (\text{uint32\_t})((\text{au->pts\_ms} - \text{pts\_start\_ms}) \times 90)$$
   (при 10 FPS шаг времени составляет 100 мс, то есть $+9000$ тиков на кадр).
3. **Критический инвариант:** Все Single NAL и все FU-A фрагменты всех NAL данного кадра получают в точности одно и то же значение $T_{\text{frame}}$.

### 4.4. Исходящий составной RTCP Compound Packet (RFC 3550)

Каждые 1000 мс монотонного времени формируется и отправляется на порт `5005` составной RTCP-пакет:

1. **Sender Report (SR, PT=200, 28 байт):**
   - Байт 0: `0x80` (`V=2`, `P=0`, `RC=0` reception report blocks)
   - Байт 1: `200` (`PT=200`, RTCP SR)
   - Байты 2–3: `htons(6)` (длина в 32-битных словах минус 1: $(28 / 4) - 1 = 6$)
   - Байты 4–7: `htonl(ssrc)`
   - Байты 8–11: NTP Timestamp MSW (секунды с 1 января 1900 года). Вычисляется через `GetSystemTimeAsFileTime` или `time(NULL) + 2208988800ULL`.
   - Байты 12–15: NTP Timestamp LSW (дробная часть секунды, $2^{32}$ долей).
   - Байты 16–19: `htonl(current_rtp_timestamp)`
   - Байты 20–23: `htonl(stats.packets_sent)`
   - Байты 24–27: `htonl((uint32_t)(stats.bytes_sent & 0xFFFFFFFF))`
2. **Source Description (SDES, PT=201):**
   - Байт 0: `0x81` (`V=2`, `P=0`, `SC=1` chunk)
   - Байт 1: `201` (`PT=201`, RTCP SDES)
   - Байты 2–3: `htons(length_in_words)`
   - Байты 4–7: `htonl(ssrc)`
   - Item 1 (CNAME):
     - Байт 8: `1` (Type = CNAME)
     - Байт 9: `cname_len`
     - Байты 10..(10 + `cname_len` - 1): строка CNAME
   - Завершающий Item:
     - Байт: `0` (Type = END)
   - Дополнение нулями (`0x00`) до кратности 4 байтам (32-битное выравнивание).
3. **RTCP BYE (PT=203, 8 байт):**
   - При уничтожении `l4c_rtp_sender_destroy` отправляется:
     `0x81, 203, htons(1), htonl(ssrc)`.
   - Отправка non-blocking / best-effort без ожидания.

### 4.5. Неблокирующий сокет Winsock и обработка ошибок (Fail-Fast)

1. **Инициализация Winsock:**
   - Вызов `WSAStartup(MAKEWORD(2, 2), &wsa_data)`.
   - Создание UDP-сокетов: `socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)`.
   - Перевод в неблокирующий режим:
     ```c
     u_long non_blocking = 1;
     ioctlsocket(sock, FIONBIO, &non_blocking);
     ```
   - Настройка размера буфера сокета:
     ```c
     int sndbuf = 256 * 1024; /* 256 KiB */
     setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char *)&sndbuf, sizeof(sndbuf));
     ```
   - **Отключение ICMP Port Unreachable сброса на Windows:**
     ```c
     #ifndef SIO_UDP_CONNRESET
     #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
     #endif
     BOOL bNewBehavior = FALSE;
     DWORD dwBytesReturned = 0;
     WSAIoctl(sock, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior),
              NULL, 0, &dwBytesReturned, NULL, NULL);
     ```
2. **Политика обработки сбоя отправки (`sendto` failure):**
   - Если вызов `sendto` возвращает `SOCKET_ERROR`:
     - Запросить `err = WSAGetLastError()`.
     - Если `err == WSAEWOULDBLOCK` или любая иная сетевая ошибка:
       - **Немедленно прервать отправку текущего AU.**
       - Оставшиеся пакеты текущего кадра **отбрасываются**.
       - Инкрементировать `stats.transport_drops++`.
       - Вызвать `encoder->force_idr()`: следующий закодированный кадр обязан стать IDR (SPS + PPS + IDR), чтобы восстановить целостность декодера браузера после потери пакетов.
       - Вернуть код ошибки `L4C_ERR_NETWORK` или `L4C_OK` с фиксацией drop в статистике. Никаких очередей в памяти!

---

## 5. Структура файлов и обновлений в `tools/l4capture/`

```text
tools\l4capture\
├── include\
│   └── l4capture\
│       ├── rtp_sender.h         # [НОВЫЙ] Публичный C API RTP/RTCP модуля
│       ├── rtp_packetizer.h     # [НОВЫЙ] RFC 6184 Single NAL и FU-A структуры
│       └── ... (существующие заголовки)
├── src\
│   ├── network\
│   │   ├── rtp_sender.c         # [НОВЫЙ] Управление сокетами, send_au, drop handling, force_idr
│   │   ├── rtp_packetizer.c     # [НОВЫЙ] Пакетизация NAL/FU-A, заголовки, marker, timestamp
│   │   └── rtcp_sender.c        # [НОВЫЙ] Построение составных пакетов RTCP SR, SDES CNAME, BYE
│   ├── pipeline\
│   │   └── pipeline.c           # [ОБНОВЛЕНИЕ] Сквозная передача: OpenH264 AU -> rtp_sender
│   └── main.c                   # [ОБНОВЛЕНИЕ] CLI флаги портов, EVENT_READY по первому IDR
├── tests\
│   ├── test_runner.c            # [ОБНОВЛЕНИЕ] Регистрация 12 тестов сетевого транспорта
│   └── test_rtp_sender.c        # [НОВЫЙ] Тестовый набор RTP, FU-A, RTCP, сокетов и loopback
├── build.cmd                    # [ОБНОВЛЕНИЕ] Компиляция src/network/*.c, линковка Ws2_32.lib
└── test.cmd                     # [ОБНОВЛЕНИЕ] Запуск расширенного тестового набора
```

---

## 6. Тестовая стратегия и автономная валидация (`tests/`)

В файле `tests/test_rtp_sender.c` реализуй 12 обязательных тестов, полностью подтверждающих соблюдение RFC 3550, RFC 6184 и архитектурных инвариантов:

1. **`test_rtp_single_nal_small` (Маленький NAL $\le 1200$ байт):**
   - Передача NAL размером 30 байт (SPS) и 8 байт (PPS).
   - Проверка, что формируется ровно 1 Single NAL пакет на каждый NAL.
   - Проверка заголовка RTP: `V=2`, `PT=96`, длина UDP payload = $12 + \text{длина NAL}$.
   - Проверка побайтового совпадения полезной нагрузки.
2. **`test_rtp_boundary_1200_1201` (Граничные размеры 1200 и 1201 байт):**
   - NAL размером ровно 1200 байт $\implies$ Single NAL packet (1 пакет, размер 1212 байт).
   - NAL размером ровно 1201 байт $\implies$ FU-A fragmentation: ровно 2 пакета:
     - Пакет 1: 12 (RTP) + 2 (FU-A) + 1198 = 1212 байт (`S=1, E=0`);
     - Пакет 2: 12 (RTP) + 2 (FU-A) + 2 = 16 байт (`S=0, E=1`).
3. **`test_rtp_fua_fragmentation_large` (Большой IDR-кадр):**
   - NAL размером 10 000 байт $\implies$ серия FU-A пакетов (9 пакетов).
   - Проверка битов:
     - Пакет 0: `FU indicator` = `(hdr & 0xE0) | 28`, `FU header` = `0x80 | (hdr & 0x1F)` (`S=1, E=0`);
     - Пакеты 1..7: `FU header` = `0x00 | (hdr & 0x1F)` (`S=0, E=0`);
     - Пакет 8: `FU header` = `0x40 | (hdr & 0x1F)` (`S=0, E=1`).
   - Проверка, что ни один пакет не превышает 1212 байт.
4. **`test_rtp_marker_bit_au_boundary` (Маркер конца кадра):**
   - Подача AU из трёх NAL: SPS (30 байт), PPS (8 байт), IDR (5000 байт, 5 фрагментов FU-A) $\implies$ всего 7 RTP-пакетов.
   - Проверка бита Marker (`M`):
     - Пакеты 0..5: `M = 0`;
     - Пакет 6 (последний фрагмент последнего NAL кадра): `M = 1`.
5. **`test_rtp_timestamp_consistency` (Единый timestamp кадра):**
   - Все 7 пакетов кадра из предыдущего теста обязаны иметь строго одинаковый RTP timestamp.
   - Следующий AU с шагом PTS +100 мс обязан иметь timestamp, увеличенный ровно на $100 \times 90 = 9000$.
6. **`test_rtp_sequence_monotonicity_and_wrap` (Инкремент и переполнение Sequence):**
   - Проверка строго монотонного инкремента `seq_num` на каждом пакете.
   - Инициализация `seq_num = 65534`, отправка 3 пакетов: проверка корректного перехода $65534 \to 65535 \to 0 \to 1$ (wrap modulo $2^{16}$).
7. **`test_rtp_timestamp_wrap` (Переполнение Timestamp):**
   - Инициализация timestamp близко к $2^{32} - 1$, проверка корректного wrap-around modulo $2^{32}$ без целочисленного сбоя.
8. **`test_rtcp_sr_sdes_generation` (Генерация составного пакета RTCP):**
   - Вызов генератора RTCP Compound Packet.
   - Проверка SR: `V=2, PT=200`, длина 28 байт, ненулевые NTP timestamp, соответствующий RTP timestamp, счётчики пакетов и октетов.
   - Проверка SDES: `V=2, PT=201`, наличие CNAME, корректное выравнивание по 32 бита.
9. **`test_rtcp_bye_generation` (Генерация RTCP BYE):**
   - Проверка формата пакета BYE (`PT=203`), соответствие SSRC.
10. **`test_rtp_fua_reassembly_roundtrip` (Круговой тест дефрагментации):**
    - Синтетический NAL случайных байт (например, 7500 байт) фрагментируется в FU-A.
    - Тестовый приёмник выполняет обратную сборку NAL: склеивает payload фрагментов, восстанавливает заголовочный байт NAL из `(fu_indicator & 0xE0) | (fu_header & 0x1F)`.
    - Сравнение собранного NAL с исходным: **100% побайтовое совпадение (0 ошибок)**.
11. **`test_network_nonblocking_drop_on_error` (Сброс AU при переполнении сокета):**
    - Эмуляция возврата `SOCKET_ERROR` / `WSAEWOULDBLOCK` при отправке середины AU.
    - Проверка: отправка оставшихся пакетов AU прекращается, `transport_drops` увеличивается на 1, взводится запрос `force_idr`.
12. **`test_pipeline_e2e_loopback` (Сквозной тест GDI $\to$ OpenH264 $\to$ RTP):**
    - Инициализация полного конвейера в тесте.
    - Открытие тестового UDP-сокета на `127.0.0.1:5004`.
    - Подача тестового кадра Color Bars $\to$ кодирование $\to$ отправка RTP.
    - Приём пакетов на тестовом сокете, валидация приёма первого IDR-кадра (наличие SPS, PPS, IDR).

---

## 7. Пошаговый алгоритм выполнения агентом (Agent Workflow)

Агент выполняет реализацию строго по следующим этапам:

1. **Этап 1: Contract Gate & Контроль допуска**
   - Открыть `docs/l4capture/prompts/contract-handoff.md`.
   - Проверить наличие принятого блока `H-L4C-03-v1` (`status: ACCEPTED`).
   - Сверить контрольные суммы 4 артефактов `H-L4C-03-v1`.
2. **Этап 2: Заголовочные файлы сетевого модуля**
   - Создать `include/l4capture/rtp_sender.h` и `include/l4capture/rtp_packetizer.h`.
3. **Этап 3: Реализация RFC 6184 пакетизатора**
   - Реализовать `src/network/rtp_packetizer.c`: упаковка Single NAL, FU-A фрагментация, расчёт заголовков, маркера и timestamp.
4. **Этап 4: Реализация RTCP генератора**
   - Реализовать `src/network/rtcp_sender.c`: построение compound пакетов SR + SDES CNAME и BYE.
5. **Этап 5: Реализация сокетов и отправки RTP Sender**
   - Реализовать `src/network/rtp_sender.c`: инициализация Winsock, неблокирующие сокеты loopback UDP, отключение `SIO_UDP_CONNRESET`, отправка AU, обработка drop/force_idr, сбор статистики.
6. **Этап 6: Интеграция со сквозным конвейером и `main.c`**
   - Обновить `src/pipeline/pipeline.c`: подключение `rtp_sender` после `openh264_encoder`.
   - Обновить `src/main.c`: приём параметров `--rtp-port`, `--rtcp-port`, отправка `EVENT_READY` при отправке первого IDR-кадра, обработка таймера RTCP SR.
7. **Этап 7: Разработка тестового набора**
   - Реализовать `tests/test_rtp_sender.c` со всеми 12 тестами.
   - Зарегистрировать тесты в `tests/test_runner.c`.
8. **Этап 8: Сборка, линковка и верификация**
   - Обновить `build.cmd` и `test.cmd`: компиляция `src/network/*.c`, линковка с `Ws2_32.lib`.
   - Собрать Release x86 и x64 (`build.cmd all`). Проверить `/W4 Zero Warnings`.
   - Запустить тесты (`test.cmd`). Убедиться в прохождении всех тестов (0 failures).
   - Проверить `dumpbin /dependents` для `bin\x86\l4capture.exe` и `bin\x64\l4capture.exe` (только системные библиотеки + `WS2_32.dll`).
9. **Этап 9: Оформление отчёта и Candidate Handoff**
   - Сформировать отчёт `docs/l4capture/handoffs/L4C-04-RTP-SENDER-report.md`.
   - Рассчитать SHA-256 реальных байтов отчёта.
   - Сформировать кандидат `docs/l4capture/handoffs/L4C-04-RTP-SENDER-candidate.md` в формате `DETACHED_V1`.

---

## 8. Критерии приёмки (Definition of Done)

Шаг считается завершенным только при одновременном выполнении следующих условий:

1. **Компиляция и сборка:**
   - Бинарники `bin\x86\l4capture.exe` и `bin\x64\l4capture.exe` успешно собираются через `build.cmd` с флагом `/MT`.
   - `bin\l4capture.exe` идентичен `bin\x86\l4capture.exe`.
   - Компиляция проходит без единого предупреждения при уровне `/W4`.
   - Таблица импортов (`dumpbin /dependents`): только системные библиотеки (`KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ADVAPI32.dll`, `OLE32.dll`, `WS2_32.dll`). Полное отсутствие внешних VC Runtime DLL.
2. **Соответствие RFC 6184:**
   - NAL $\le 1200$ байт упаковываются в Single NAL Unit пакеты.
   - NAL $> 1200$ байт фрагментируются в FU-A с полезной нагрузкой каждого фрагмента $\le 1198$ байт.
   - Бюджет RTP payload строго $\le 1200$ байт.
   - Бит Marker ($M=1$) выставляется строго на последнем RTP-пакете Access Unit.
   - Все RTP-пакеты одного Access Unit имеют идентичный RTP timestamp.
3. **Исходящий RTCP (RFC 3550):**
   - Формируются и отправляются compound RTCP пакеты (SR + SDES CNAME) с интервалом 1.0 с.
   - Корректная трансляция системного времени в NTP timestamp (смещение 2208988800).
   - Пакет RTCP BYE отправляется при штатном завершении (best effort).
4. **Сетевая устойчивость (Fail-Fast):**
   - Сокеты UDP работают в неблокирующем режиме на `127.0.0.1:5004`/`5005`.
   - При ошибке сокета остаток кадра сбрасывается без блокировки, и взводится запрос `force_idr()`.
   - Отсутствие утечек сокетов и дескрипторов при завершении.
5. **Сквозной конвейер:**
   - Кадр GDI захватывается, масштабируется, конвертируется в I420, кодируется OpenH264 и отправляется по RTP в loopback сокет.
   - При отправке первого ключевого кадра (SPS+PPS+IDR) генерируется событие `EVENT_READY`.
6. **Тестовое покрытие:**
   - Все тесты в `tests/` выполняются успешно через `test.cmd` (0 failures, 0 errors).
7. **Оформление handoff:**
   - Файлы `l4desk-service` не затронуты.
   - Отчёт и кандидат подготовлены строго в каталоге `docs/l4capture/handoffs/`.

---

## 9. Оформление отчёта и Candidate Handoff (`DETACHED_V1`)

По завершении всех работ сформируй два артефакта согласно стандарту `PROMPT-STANDARD.md`:

### 1. Отчёт исполнителя: `docs/l4capture/handoffs/L4C-04-RTP-SENDER-report.md`
Должен содержать:
- Статус: строго `ACCEPTED`.
- Ветка и SHA коммита реализации (`producer_commit`).
- Полный перечень созданных и изменённых файлов в `tools/l4capture/`.
- Вывод выполнения `build.cmd all` и `test.cmd`.
- Результаты проверки RFC 6184 (Single NAL, FU-A, маркер AU, единый timestamp).
- Результаты проверки RTCP SR/SDES/BYE.
- Результаты теста неблокирующего сокета и сквозного конвейера loopback.
- Вывод `dumpbin /dependents` для x86 и x64 (подтверждение наличия `WS2_32.dll` и отсутствия runtime DLL).

### 2. Файл кандидата: `docs/l4capture/handoffs/L4C-04-RTP-SENDER-candidate.md`
Вычисли SHA-256 хеш реальных байтов отчёта (PowerShell: `(Get-FileHash -Algorithm SHA256 docs\l4capture\handoffs\L4C-04-RTP-SENDER-report.md).Hash.ToLower()`).

Файл кандидата оформляется строго в формате:

```markdown
<!-- HANDOFF:H-L4C-04-v1:BEGIN -->
```yaml
handoff_id: H-L4C-04-v1
status: ACCEPTED
contract_kinds:
  - RTP_SENDER
  - RFC_6184_PACKETIZER
  - FUA_FRAGMENTATION
  - RTCP_SENDER_REPORT
  - UDP_LOOPBACK_TRANSPORT
  - E2E_MEDIA_PIPELINE
producer_prompt_id: L4C-04-RTP-SENDER
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-04-RTP-SENDER-report.md
producer_branch: l4capture/l4c-04-rtp-sender
producer_commit: <GIT_COMMIT_SHA>
accepted_at_utc: <ISO_8601_TIMESTAMP>
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-04-RTP-SENDER-report.md
  - tools/l4capture/include/l4capture/rtp_sender.h
  - tools/l4capture/include/l4capture/rtp_packetizer.h
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - <SHA256_REPORT>
  - <SHA256_RTP_SENDER_H>
  - <SHA256_RTP_PACKETIZER_H>
  - <SHA256_BIN_X86_EXE>
  - <SHA256_BIN_X64_EXE>
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
  breaking_changes: false
  notes: Реализация передачи H.264 по RTP/RTCP (RFC 3550, RFC 6184 FU-A). Неблокирующий UDP loopback 127.0.0.1:5004/5005, маркер AU, каденция RTCP SR/SDES раз в 1.0 с, drop AU при переполнении сокета с немедленным force-IDR.
deployment_status: LOCAL_BUILD_VERIFIED
deployed_environment: local_development
feature_flags:
  l4capture_native_pipeline: enabled
  l4capture_openh264_encoder: enabled
  l4capture_rtp_transport: enabled
contract_payload:
  rtp_transport:
    protocol: RTP/UDP (RFC 3550, RFC 6184)
    destination: 127.0.0.1
    rtp_port: 5004
    rtcp_port: 5005
    payload_type: 96
    clock_rate_hz: 90000
    socket_mode: non-blocking (FIONBIO)
    sndbuf_bytes: 262144
    sio_udp_connreset_disabled: true
  packetization:
    mode: 1 (Non-interleaved)
    max_payload_bytes: 1200
    single_nal_max_bytes: 1200
    fu_a_max_payload_bytes: 1198
    fu_a_indicator_type: 28
    marker_bit_semantics: 1 strictly on last packet of Access Unit
    timestamp_semantics: identical for all packets of the same AU
  rtcp_feedback:
    sender_report_interval_ms: 1000
    sdes_cname_present: true
    bye_on_destroy: true (best effort)
    pli_guard: rate limited <= 1 per 500ms
  resilience_and_safety:
    socket_error_policy: drop remaining AU, increment transport_drops, trigger force_idr
    queue_policy: zero queue accumulation, bounded buffers
    memory_allocation: zero dynamic allocations during streaming
supersedes: []
known_risks:
  - R2: Нарушение доставки/потери — периодический IDR каждые 2.0 с и force_idr при сбоях восстанавливают поток
  - R3: Переполнение сетевых буферов — неблокирующий сокет с мгновенным сбросом AU исключает рост задержки
consumers:
  - L4C-05-AGENT-ADAPTER
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
next_prompt_id: L4C-05-AGENT-ADAPTER
```
<!-- HANDOFF:H-L4C-04-v1:END -->
```
