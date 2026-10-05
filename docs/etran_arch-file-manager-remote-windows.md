# Файловый менеджер удалённых Windows-устройств: архитектура и S3-транспорт

**Статус:** проектное решение (design) + верификация cloud.ru Object Storage  
**Дата:** 2026-10-05  
**UI-референс:** `l4desk-service/docs/file-manager.png` (ISPmanager File Manager)  
**Владелец product/BFF:** `MenuBuilder` · **RPC/session:** `app1` (`iot-rpc-rest-app`) · **Агент:** `tools/l4con` (FS-модуль) · **Bulk plane:** cloud.ru S3  

---

## 1. Задача

Отдельный раздел **MenuBuilder** для удалённых Windows-машин:

1. Навигация по дискам/папкам (ленточки breadcrumbs, таблица файлов).
2. **Скачать** файл с терминала на локальную машину оператора.
3. **Загрузить** файл с локальной машины оператора на терминал.

UX-ориентир — «Менеджер файлов» ISPmanager: toolbar, breadcrumbs, таблица (имя/размер/дата/атрибуты), multi-select и upload/download. Для Windows POSIX-права/владелец не нужны — атрибуты (`A/H/R/S`) и тип (dir/file).

**Категорически не используется** (решение 2026-10-05):

- перенос файлов **чанками через MQTT broker**;
- перенос через **консоль `7001` / PowerShell / certutil**.

Принято: **гибрид** — сигнализация RPC/MQTT + надёжный bulk-транспорт (S3 presigned / HTTPS).

---

## 2. Отклонённые варианты

| Вариант | Вердикт | Почему |
|---|---|---|
| **A. Всё через MQTT RPC** (base64-чанки `out`) | ❌ | ~3 КБ на чанк, base64 +33%, нагрузка broker, слабый resume; command_line ≤ 4096 chars |
| **C. Через консоль 7001 + shell** | ❌ | blacklist опасных команд, нет структурированных ошибок, ломает model Job/exec, «не продукт» |
| **B. Гибрид: MQTT control + отдельный bulk** | ✅ | масштаб, resume, аудит, offload серверов |

Уточнение B: bulk — **временные объекты в S3 + presigned URL** (cloud.ru), при недоступности S3 у киоска — fallback `app1_http`.

---

## 3. Сквозная архитектура

```text
┌──────────────────┐  REST/WS JWT   ┌────────────────────┐  internal REST  ┌──────────────────────┐
│ MenuBuilder FE   │───────────────►│ MenuBuilder BFF    │────────────────►│ app1 (iot-rpc-rest)  │
│ DeviceFilesTab   │◄───────────────│ lease, audit, ACL  │◄────────────────┤ ticket + S3 signing  │
│ (file manager UI)│  listing/json  │ presign broker     │  task_id/RSP    │ MQTT RPC publisher   │
└────────┬─────────┘                └────────────────────┘                 └──────────┬───────────┘
         │ presigned GET/PUT                                                         │ 7020/7021/7022
         │ (browser ⇄ S3)                                                            ▼
         ▼                                                                 ┌──────────────────────┐
┌──────────────────┐   HTTPS WinHTTP PUT/GET / multipart / Range            │ l4con FS engine      │
│ cloud.ru S3      │◄──────────────────────────────────────────────────────►│ native Win32 FS API  │
│ bucket           │   progress POST /file-transfers/{id}/progress          │ + policy + sha256    │
│ l4desk-fm-temp   │                                                        └──────────────────────┘
└──────────────────┘
```

### Plane map

| Plane | Транспорт | Payload |
|---|---|---|
| **Signaling** | MQTT RPC `srv/{SN}/tsk|rsp|cmt` ↔ `dev/{SN}/req|res` | list, transfer_start/cancel, итог, capability |
| **Bulk** | S3 (cloud.ru Object Storage), presigned URL | байты файла |
| **Progress** | HTTPS агент → app1 REST | bytes_done/total, throttled ~0.5–1 s |
| **UI ↔ BFF** | HTTPS REST/WS, JWT | listing, transfer tickets, download URL |

Матрица MQTT **не расширяется** ad-hoc топиками. Progress не идёт через `dev/{SN}/out`.

---

## 4. RPC-контракт (резерв `7000–7099`)

Старый агент без capability `7020..` получает `501`; UI показывает «Файловый менеджер недоступен».

| Метод | Назначение | Ключевые поля `payload.dt[0]` |
|---|---|---|
| **7020** `FS_LIST` | Диски + листинг папки | `path?` (`null`/empty → drives) |
| **7021** `FS_TRANSFER` | Старт/исполнение transfer | `transfer_id`, `direction` (`to_browser`\|`to_device`), `path`, `transport` (`s3`\|`app1_http`), `url`, `parts?`, `size?`, `sha256?`, `ttl_sec` |
| **7022** `FS_TRANSFER_CANCEL** | Отмена активного transfer | `transfer_id` |
| **7023** *(optional)* `FS_STAT` | Metadata одного объекта | `path` |

Итог — штатные `RES` (`status_code` 200/403/404/409/413/500), `result_uid`. Ошибки агента в структурированном `error_code`:

`access_denied`, `not_found`, `not_a_file`, `policy_denied`, `too_large`, `disk_full`, `transfer_busy`, `s3_http_error`, `checksum_mismatch`, `cancelled`, `timeout`.

### 4.1. FS_LIST response (пример)

```json
{
  "id": "<task_uuid>",
  "header": {"method_code": 7020},
  "payload": {"dt": [{
    "path": "C:\\Users\\Public",
    "drives": false,
    "entries": [
      {
        "name": "Documents",
        "type": "dir",
        "size": null,
        "mtime_utc": "2026-10-01T12:00:00Z",
        "attrs": "D"
      },
      {
        "name": "report.xml",
        "type": "file",
        "size": 20480,
        "mtime_utc": "2026-10-04T09:30:00Z",
        "attrs": "A"
      }
    ]
  }]}
}
```

Drives (`path` empty):

```json
{"dt": [{
  "drives": true,
  "entries": [
    {"name": "C:\\", "type": "drive", "label": "System", "size_total": 128034704896, "size_free": 42000000000}
  ]
}]}
```

### 4.2. FS_TRANSFER (7021) — ticket в RSP

Как `7011`: секретные/URL-поля только в RSP на устройство, в истории/логах маскируются.

```json
{
  "id": "<task_uuid>",
  "header": {"method_code": 7021},
  "payload": {"dt": [{
    "transfer_id": "5f0c…",
    "direction": "to_browser",
    "path": "C:\\logs\\app.log",
    "transport": "s3",
    "ttl_sec": 3600,
    "max_bytes": 524288000,
    "s3": {
      "endpoint": "https://s3.cloud.ru",
      "region": "ru-central-1",
      "bucket": "l4desk-fm-temp",
      "key": "fm/<tenant>/<sn>/<transfer_id>/app.log",
      "method_put_url": "https://l4desk-fm-temp.s3.cloud.ru/fm/…?X-Amz-…",
      "multipart": {
        "part_size": 8388608,
        "create_url": "…",
        "upload_part_urls": ["…partNumber=1…", "…partNumber=2…"],
        "complete_url": "…",
        "abort_url": "…"
      }
    },
    "sha256_expected": null
  }]}
}
```

`direction=to_device`: зеркально `method_get_url` (агент), `method_put_url` (браузер).

---

## 5. Потоки операций

### 5.1. Download (терминал → локальная машина)

1. UI: `GET /api/v1/devices/{id}/files?path=C:\\logs` → BFF → app1 → MQTT `7020` → JSON listing.
2. UI «Скачать» → BFF → app1: create `transfer_id`, S3 key `fm/{tenant}/{sn}/{transfer_id}/{name}`, выдаёт **PUT**-presign (агенту) и регистрирует transfer.
3. MQTT `7021` `direction=to_browser` с PUT URL / multipart URLs.
4. Агент: `CreateFileW` read → `PUT`/`UploadPart` в S3; progress POST на app1.
5. `RES` `200` + `bytes`, `sha256`.
6. app1 отдаёт BFF/браузеру короткоживущий **GET**-presign → `location.href` / fetch download.

### 5.2. Upload (локальная машина → терминал)

1. UI «Загрузить» + target path → app1: `transfer_id`, **PUT**-presign (браузер), **GET**-presign (агент).
2. Браузер `PUT`/multipart в S3 (`XMLHttpRequest`/`fetch`).
3. MQTT `7021` `direction=to_device`: GET URL + dest path + `sha256?`.
4. Агент: GET → write во временный `*.partial` → `MoveFileExW` → verify sha256.
5. `RES` ok / `access_denied` / `disk_full` / `policy_denied`.

Объекты **TTL 1 сутки** (lifecycle prefix `fm/`) + явный `DeleteObject` после успеха. Orphan MPU — `AbortIncompleteMultipartUpload` lifecycle (1 day) + job `ListMultipartUploads` в app1.

### 5.3. Transport fallback

| Условие | `transport` |
|---|---|
| Киосок имеет egress к `s3.cloud.ru:443` | `s3` |
| Корпоративный firewall режет S3 | `app1_http` (stream через app1, тот же ticket/контракт) |
| Файл < 256 КБ | MVP: тоже `s3` (консистентность); позже optional inline |

---

## 6. Агент: обязательный FS-контур

Отдельный модуль внутри `l4con` (не shell, не `dir`), **заточенный под file manager**:

| Блок | Реализация |
|---|---|
| Listing | `GetLogicalDrivesW`, `GetDriveTypeW`, `FindFirstFileW`/`FindNextFileW` |
| Read/Write | `CreateFileW`, sequential/chunked I/O; write через `.partial` + `MoveFileExW` |
| HTTP | WinHTTP (как `l4setup`/`l4pin`): `PUT`/`GET`/`UploadPart`/`AbortMultipartUpload` |
| Integrity | `BCrypt` SHA-256; optional AES-256-GCM (SSE-C-style E2E) |
| Job | TTL, cancel (`7022`), **один** активный transfer на SN |
| Progress | HTTPS POST `/api/internal/v1/file-transfers/{id}/progress` |
| Capability | `rpc_methods` += `7020,7021,7022` |

### 6.1. Политика путей (жёстко)

**Deny (read и write):**

- `C:\Windows`, `C:\Program Files`, `C:\Program Files (x86)`
- `pagefile.sys`, `hiberfil.sys`, `swapfile.sys`
- `*.pfx`, `*.p12`, `*.key`, `*.pem` (опционально), `NTUSER.DAT`
- точки восстановления, `System Volume Information`

**Deny write** в любые system dirs; allow write только в явно выбранный path + descendants.  
**Path normalize:** отклонять `..`, alternate data streams (`file:stream`), device paths `\\.\`, UNC без whitelist.

**Lease:** scope `files` (по образцу remote-input) — один оператор на SN, TTL + keepalive.

**Audit (server):** user_id, tenant_id, SN, path, direction, bytes, sha256, transfer_id, transport, result.

### 6.2. Firewall (`l4setup`)

Наряду с существующими `L4Tools-*`:

- outbound **TCP 443** к `s3.cloud.ru` (host-based rule / allow program `l4con.exe`);
- smoke-проба: tiny presigned PUT/GET (аналог `_leo4/info`);
- Win7: TLS 1.2 Schannel уже предусмотрен в Preflight.

Прецедент прямого HTTPS-egress к cloud.ru уже принят: Generic AR `l4tools-generic.ar.cloud.ru` (WinHTTP, без секретов на терминале).

---

## 7. MenuBuilder UI

Отдельный tab `DeviceFilesTab` на карточке устройства (`sys=windows`), рядом с `DeviceConsoleTab`:

- Breadcrumbs + drive switcher;
- Таблица: имя, тип, размер, изменён, атрибуты;
- Toolbar MVP: **Обновить · Скачать · Загрузить**;
- Панель transfer: progress, cancel, retry;
- Фаза 2: multi-select, drag-and-drop, mkdir/rename/delete, search.

BFF: JWT, tenant/device ownership, lease `files`, проксирует listing; **не** тащит bulk при `transport=s3`. Browser получает только presigned URL (или stream через BFF при fallback).

---

## 8. Верификация cloud.ru Object Storage API

Источник: <https://cloud.ru/docs/s3e/ug/topics/api> (и подстраницы methods / headers / sig-v4 / lifecycle / encryption).

| Возможность | Статус | Применение в FM |
|---|---|---|
| Endpoint `https://s3.cloud.ru`, region `ru-central-1` | ✅ | egress |
| Access Key format `tenant_id:key_id` + secret | ✅ | только у app1 |
| AWS SigV4 | ✅ | presign + server API |
| **Presigned URL** (`X-Amz-Signature`, `X-Amz-Expires` 1…604800) | ✅ | core |
| PutObject / GetObject / HeadObject | ✅ | single-shot |
| GetObject `Range` | ✅ | resume download |
| Multipart: Create / UploadPart / Complete / Abort / ListParts / ListMultipartUploads | ✅ | большие файлы, resume upload |
| Part numbers 1…10000, last-wins | ✅ | retry part |
| Checksums CRC32/CRC32C/SHA1/**SHA256** | ✅ | integrity |
| Lifecycle Expiration (Prefix + Days) + NoncurrentVersionExpiration | ✅ | TTL `fm/*` |
| CORS (Put/Get/DeleteBucketCors) | ✅ | browser PUT/GET |
| Public Access Block | ✅ | private-only |
| Bucket Policy / ACL / Versioning / Object Lock / Tagging | ✅ | ACL/PAB достаточно |
| SSE-C / SSE-KMS | ✅ | optional E2E |
| CopyObject (source ≤ 5 GB) | ✅ | не обязательно |
| `aws:SecureTransport` condition | ❌ `NotImplemented` | HTTPS-only policy не ставится |
| Два lifecycle-правила с одним Prefix | ❌ `InvalidRequest` | merge expire + abort MPU |
| Deny `Principal:*` при BlockPublicPolicy | ❌ `AccessDenied` | ожидаемо; PAB закрывает public |

**Auth key (server only):**

```text
AWS Access Key ID     = <S3_TENANT_ID>:<S3_KEY_ID>
AWS Secret Access Key = <S3_KEY_SECRET>
Default region name   = ru-central-1
Endpoint              = https://s3.cloud.ru
```

Секреты живут в `s3_cloud_ru.env` (в `.gitignore`), **никогда** на терминале и в Git.

---

## 9. Бенчмарк (2026-10-05, файл `tools/dist/l4setup.exe`, 29.75 MB)

Локальный sha256: `3ee234ff1f5ab716a0de43a1b9d26303f9ad11432adc939bbb1e7d73cc1960ac` (round-trip match).

### 9.1. Подготовка (one-time per bucket)

| Операция | Время |
|---|---|
| HeadBucket | 1.76 s (first TLS+auth) |
| PutBucketLifecycle | 0.32 s |
| PutBucketCors | 0.35 s |
| PutPublicAccessBlock | ~0.3 s |
| CreateBucket | ~1–2 s |

### 9.2. Перенос 29.75 MB

| Операция | Прогон 1 | Прогон 2 | Скорость |
|---|---|---|---|
| PutObject (single) | **2.81 s** | 3.77 s | **7.9–10.6 MB/s** |
| GetObject (single) | **3.71 s** | 39.4 s * | 0.75–8.0 MB/s |
| Multipart 8 MB × 4 | 4.93 s | — | медленнее single на 30 MB |
| Range GET (2nd half) | 4.47 s | — | resume работает |
| Presign GET → plain HTTP | **4.23 s** | — | sha256 match |
| Presign PUT → plain HTTP (tiny) | 1.60 s | — | HTTP 200 |
| Presign generate (local) | 0.002 s | — | |

\* сетевой выброс; соседний presign GET — 4.2 s. Ориентир: **~3–5 s / 30 МБ (~8 MB/s)**, запас до ×10 при jitter.

### 9.3. RTT tiny object

put 0.25 s · get 0.21 s · head 0.15 s · list prefix 0.13 s

### 9.4. Практические выводы

1. Presign + plain HTTP (без AWS SDK) — рабочий путь **WinHTTP-агента**.
2. 100 МБ ≈ 12–15 s; 500 МБ ≈ 1–1.5 min (норма), с запасом на канал.
3. Multipart включать от ~32–64 МБ, part 8–16 MB, 2–3 concurrent parts.
4. Range готов для resume download в браузер.
5. Prep (lifecycle/CORS/PAB) — доли секунды, вне runtime.

---

## 10. Production bucket `l4desk-fm-temp` (настроен 2026-10-05)

| Параметр | Значение |
|---|---|
| Bucket | `l4desk-fm-temp` |
| Endpoint | `https://s3.cloud.ru` (virtual-host: `l4desk-fm-temp.s3.cloud.ru`) |
| Public Access Block | все 4 = `true` |
| Lifecycle | `fm-temp-cleanup`, prefix `fm/`, Expiration 1 day, AbortIncompleteMultipartUpload 1 day |
| CORS `fm-browser-transfer` | GET/PUT/HEAD/POST, `AllowedHeaders=*`, ExposeHeaders `ETag`, `x-amz-checksum-sha256`, MaxAge 3600 |
| Bucket policy | не применён (SecureTransport unsupported; Deny Principal:* режется PAB) |

**CORS origins:**

```text
https://l4desk.ru
https://www.l4desk.ru
https://dev.leo4.ru
https://dev.leo4.ru:4443
https://dev.leo4.ru:3000
https://dev.leo4.ru:1443
http://176.108.247.249
http://176.108.247.249:80
http://176.108.247.249:3000
http://176.108.247.249:8080
http://176.108.247.249:5173
https://176.108.247.249
https://176.108.247.249:3000
https://87.242.100.34:3000
https://87.242.100.34:1443
https://87.242.100.34:1444
```

`176.108.247.249` — builder (test origin). Production UI также на `87.242.100.34:*`.

**Smoke:** PUT/GET `fm/setup-smoke.txt`, presign GET — OK.

---

## 11. Образцы кода работы с S3

### 11.1. Клиент boto3 (app1 / скрипты) — env без вывода секретов

```python
from pathlib import Path
import boto3
from botocore.config import Config

def load_env(path: Path) -> dict[str, str]:
    data: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, _, v = line.partition("=")
        data[k.strip()] = v.strip().strip('"').strip("'")
    return data

env = load_env(Path("s3_cloud_ru.env"))  # S3_KEY_ID, S3_KEY_SECRET, S3_TENANT_ID, S3_ENDPOINT
endpoint = env["S3_ENDPOINT"]
if not endpoint.startswith("http"):
    endpoint = "https://" + endpoint

s3 = boto3.client(
    "s3",
    endpoint_url=endpoint,
    aws_access_key_id=f"{env['S3_TENANT_ID']}:{env['S3_KEY_ID']}",  # cloud.ru format
    aws_secret_access_key=env["S3_KEY_SECRET"],
    region_name="ru-central-1",
    config=Config(
        signature_version="s3v4",
        s3={"addressing_style": "virtual"},
        retries={"max_attempts": 3, "mode": "standard"},
        connect_timeout=15,
        read_timeout=300,
    ),
)
```

### 11.2. Presigned GET/PUT (ticket для агента / браузера)

```python
BUCKET = "l4desk-fm-temp"
key = f"fm/{tenant_id}/{sn}/{transfer_id}/{safe_name}"

url_put = s3.generate_presigned_url(
    "put_object",
    Params={"Bucket": BUCKET, "Key": key},
    ExpiresIn=900,  # 15 min; для multipart — отдельные part URLs
)
url_get = s3.generate_presigned_url(
    "get_object",
    Params={"Bucket": BUCKET, "Key": key},
    ExpiresIn=900,
)
# Передавать только по TLS в MQTT RSP (агенту) / HTTPS (браузеру).
# В history/логах маскировать query X-Amz-*.
```

### 11.3. Single upload + sha256 metadata (app1 или утилита)

```python
import hashlib
from pathlib import Path

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

file_path = Path("tools/dist/l4setup.exe")
digest = sha256_file(file_path)

s3.upload_file(
    str(file_path),
    BUCKET,
    key,
    ExtraArgs={
        "ContentType": "application/octet-stream",
        "Metadata": {"transfer-id": transfer_id, "local-sha256": digest},
    },
)
# цикл ту-ту: download_file / get_object и сравнить sha256_file
```

### 11.4. Multipart upload (resume, part 8 MB)

```python
PART_SIZE = 8 * 1024 * 1024

def multipart_upload(local_path: Path, key: str) -> int:
    mpu = s3.create_multipart_upload(Bucket=BUCKET, Key=key,
                                     ContentType="application/octet-stream")
    upload_id = mpu["UploadId"]
    parts = []
    part_number = 1
    try:
        with local_path.open("rb") as f:
            while True:
                chunk = f.read(PART_SIZE)
                if not chunk:
                    break
                part = s3.upload_part(
                    Bucket=BUCKET, Key=key,
                    PartNumber=part_number, UploadId=upload_id, Body=chunk,
                )
                parts.append({"ETag": part["ETag"], "PartNumber": part_number})
                part_number += 1
        s3.complete_multipart_upload(
            Bucket=BUCKET, Key=key, UploadId=upload_id,
            MultipartUpload={"Parts": parts},
        )
        return len(parts)
    except Exception:
        s3.abort_multipart_upload(Bucket=BUCKET, Key=key, UploadId=upload_id)
        raise
```

### 11.5. Range GET (resume download в браузер / агент)

```python
# Bytes [offset, size)
obj = s3.get_object(Bucket=BUCKET, Key=key, Range=f"bytes={offset}-{size - 1}")
body = obj["Body"].read()
# HTTP-эквивалент: GET url_get  +  header Range: bytes=offset-
```

### 11.6. Browser upload (пресigned PUT, без AWS SDK)

```javascript
async function uploadToS3(presignedPutUrl, file, onProgress) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open("PUT", presignedPutUrl);
    xhr.setRequestHeader("Content-Type", "application/octet-stream");
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) onProgress(e.loaded / e.total);
    };
    xhr.onload = () =>
      xhr.status >= 200 && xhr.status < 300
        ? resolve()
        : reject(new Error(`S3 PUT ${xhr.status}`));
    xhr.onerror = () => reject(new Error("S3 PUT network error"));
    xhr.send(file); // File/Blob из <input type="file">
  });
}
```

### 11.7. Browser download (presigned GET)

```javascript
async function downloadFromS3(presignedUrl, filename) {
  const res = await fetch(presignedUrl);
  if (!res.ok) throw new Error(`S3 GET ${res.status}`);
  const blob = await res.blob();
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = filename;
  a.click();
  URL.revokeObjectURL(a.href);
}
```

### 11.8. WinHTTP PUT (C, sketch для l4con FS engine)

```c
/* Ticket из RSP 7021: full presigned URL уже содержит query auth.
   Не хранить Key Secret на устройстве. */
HINTERNET ses = WinHttpOpen(L"L4Con-FS/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
/* WinHttpCrackUrl(presigned) -> host/port/path+query */
HINTERNET con = WinHttpConnect(ses, host, port, 0);
HINTERNET req = WinHttpOpenRequest(con, L"PUT", path_and_query, NULL,
                                   WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   WINHTTP_FLAG_SECURE);
/* Стриминг файла частями WinHttpWriteData + SendRequest;
   sha256 через BCrypt параллельно чтению. */
```

### 11.9. Idempotent production setup (lifecycle merge)

```python
# cloud.ru: НЕЛЬЗЯ два правила с одинаковым Prefix
s3.put_bucket_lifecycle_configuration(
    Bucket="l4desk-fm-temp",
    LifecycleConfiguration={
        "Rules": [{
            "ID": "fm-temp-cleanup",
            "Prefix": "fm/",
            "Status": "Enabled",
            "Expiration": {"Days": 1},
            "AbortIncompleteMultipartUpload": {"DaysAfterInitiation": 1},
        }]
    },
)
s3.put_public_access_block(
    Bucket="l4desk-fm-temp",
    PublicAccessBlockConfiguration={
        "BlockPublicAcls": True,
        "IgnorePublicAcls": True,
        "BlockPublicPolicy": True,
        "RestrictPublicBuckets": True,
    },
)
```

---

## 12. Безопасность (сводка)

1. **Ключи S3 — только в app1** (env/secrets manager). Агент и браузер — только presigned URL (TTL 15–60 min, scope один key + method).
2. **PAB + private bucket**; без public ACL/policy.
3. **Path policy** на агенте (deny system / secrets).
4. **Lease `files`** — один оператор на SN.
5. **Audit** всех transfer.
6. **sha256** end-to-end (metadata + RES).
7. **Optional E2E (SSE-C / AES-GCM на клиенте):** DEK через MQTT RSP агенту и TLS браузеру; в S3 ciphertext. Рекомендуется до передачи логов с ПДн.
8. **Маскирование** presigned query в истории (как PIN в 7011).
9. **Limits:** max_bytes (MVP 500 МБ), max 1 transfer/SN, rate limit list ops.

---

## 13. Состав MVP и этапы

| Этап | Состав | Компоненты |
|---|---|---|
| **MVP** | FS_LIST, download 1 файла, upload 1 файла, deny-лист, lease, audit, `DeviceFilesTab` | app1, l4con FS, MenuBuilder FE/BE, l4setup firewall |
| **Этап 2** | Multipart/resume, progress UI, cancel, multi-file | те же |
| **Этап 3** | mkdir/rename/delete/search, E2E SSE-C | те же + crypto |

**Компоненты:**

| Компонент | Изменения |
|---|---|
| `app1` | `file-transfers` API, S3 signing, MQTT 702x task publish, progress consumer |
| `tools/l4con` | FS engine, RPC 7020–7022, WinHTTP S3 client, policy |
| `tools/l4setup` | firewall whitelist `s3.cloud.ru:443` |
| `MenuBuilder/backend` | BFF `/files`, `/file-transfers`, lease, audit |
| `MenuBuilder/frontend` | `DeviceFilesTab`, transfer UI |
| cloud.ru | bucket `l4desk-fm-temp` (**готов**) |

---

## 14. Риски и открытые вопросы

| # | Тема | Статус |
|---|---|---|
| 1 | Egress киосков к `s3.cloud.ru` (corp firewalls) | fallback `app1_http` обязателен |
| 2 | E2E шифрование (SSE-C / client AES-GCM) | решение: позже, до ПДн |
| 3 | Max size файла | рекомендация **500 МБ** MVP |
| 4 | `aws:SecureTransport` в cloud.ru | нет; компенсация PAB + HTTPS-only клиенты |
| 5 | AbortIncompleteMultipartUpload в lifecycle | применён merged-правилом; проверить фактический runtime |
| 6 | Точные лимиты part/object cloud.ru | до 500 МБ достаточно; уточнить при >1 GB |
| 7 | Где жить handlers: модуль l4con vs `l4fs` | рекомендация — **модуль l4con** (не плодить MQTT-клиентов) |

---

## 15. Связанные документы

- [`ops_run-remote-console-diagnostics.md`](ops_run-remote-console-diagnostics.md) — console RPC, матрица топиков.
- [`term_arch-rpc7011-flow-matrix.md`](term_arch-rpc7011-flow-matrix.md) — паттерн ticket/RSP, маскирование секретов.
- [`etran_arch-remote-input-control.md`](etran_arch-remote-input-control.md) — lease lifecycle (scope `files`).
- [`term_tool-zero-touch-installer-and-remote-runtime-plan.md`](term_tool-zero-touch-installer-and-remote-runtime-plan.md) — прямой HTTPS-egress к cloud.ru (Generic AR).
- Cloud.ru S3 API: <https://cloud.ru/docs/s3e/ug/topics/api>

---

## 16. Активы этого исследования

| Артефакт | Назначение | Git |
|---|---|---|
| `s3_cloud_ru.env` | креды S3 (local only) | **gitignore** |
| `check_s3_fm_bench.py`, `check_s3_fm_bench2.py` | бенчмарк | gitignore (`check_*.py`) |
| `check_s3_fm_prod_setup.py` | idempotent prod setup | gitignore |
| `check_s3_fm_prod_smoke.py` | smoke + policy probe | gitignore |
| Bucket `l4desk-fm-temp` | production FM temp objects | cloud.ru |
| `l4desk-service/docs/file-manager.png` | UI reference | локально |
