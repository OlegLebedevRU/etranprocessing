# Отчёт corrective-шага L4D-08A-FIX-01-MEDIA (A+E)

**Шаг:** `L4D-08A-FIX-01-MEDIA`  
**Проект:** `l4media`  
**Ветка:** `l4desk/l4d-15c-media`  
**Output Handoff:** `H-L4D-08A-FIX-01-MEDIA-v1`  
**Corrects:** `H-L4D-08A-MEDIA-v1`  
**Дата:** 2026-09-24

---

## 1. Проблема

Live freeze трансляции (~17:48 при RTP живом): статичный кадр в браузере при `STREAMING [FRESH]` на ingress.

Root cause — `janus_create_mountpoint` (`media_lifecycle.h`) при `already exists` делал **destroy + recreate**. Watcher терял WebRTC-track → статичный последний кадр. Плюс reconcile считал mountpoint/route «orphan» и уничтожал их, пока терминал ещё слал RTP (session `stopped` / `lease_released`).

## 2. Решение A+E

### A — reuse if healthy
- `janus_create_mountpoint`: `already exists` → **reuse** (return 0), destroy запрещён.
- `janus_get_mountpoint_info` — probe через Janus admin `info` (`port`/`rtcpport`/`pin`).
- `janus_mountpoint_ensure` — match → reuse; mismatch → destroy+create.
- Idempotent start и fresh start идут через `ensure`.

### E — reconcile уважает fresh RTP
- `rtp_is_fresh_for_sn`, окно `L4MEDIA_RTP_FRESH_RECONCILE_SEC = 20`.
- `find_sn_for_mountpoint` — ассоциация SN с mountpoint (включая STOPPED-сессии).
- Orphan mountpoint/route **keep** при живом RTP; prune только после протухания.

## 3. Коммиты

| SHA | |
|---|---|
| `29a113a67979e1c45233b60ddcbca31a25425b83` | feat A+E |
| `93943f1c30cc0e83980e055bfdd7bd93b419de8e` | probe via Janus `info` (list не отдаёт ports/pin) |

## 4. Live-верификация (stand 87.242.100.34)

Deploy: backup `media_lifecycle.h.bak-pre-AE-20260924`, scp, `docker compose build ingress`, `docker rm -f` + `up -d`.

**Suite `test_media_lifecycle.py` — 9/9 PASS** (OpenAPI, auth, lifecycle, idempotency+mountpoint survival, session_busy, concurrency 1 winner, orphan reconcile, TTL watchdog, metrics).

**Сигнатуры в логе l4media-ingress:**

```
[INGRESS JANUS] Mountpoint 9234 healthy, reusing (ports 6010/6011)
[INGRESS JANUS] Mountpoint 773 healthy, reusing (ports 6046/6047)
[INGRESS RECONCILE] keep route for SN test-ae-rtp-1790285305 rtp_fresh
[INGRESS RECONCILE] keep route for SN test-ae-rtp-1790285305 rtp_fresh
[INGRESS RECONCILE] Removing orphan dynamic route for SN test-ae-rtp-1790285305
[INGRESS RECONCILE] Destroying orphan Janus mountpoint 9888
```

**E probe:** orphan route + L4RTP TCP → reconcile **keep** (`orphans_cleaned_routes: 0`); через 22 s → prune (`orphans_cleaned_routes: 1`). Настоящие orphans без RTP по-прежнему чистятся.

`destroying to recreate` в логах **нет**.

## 5. Инварианты

- Внешние API lifecycle не изменены.
- Hot RTP path не тронут.
- `stop_session` по-прежнему destroy mountpoint (явный stop).
- True orphan без RTP чистится (TEST 7 PASS).
