# Отчёт corrective-шага L4D-REDIS-MEDIA-01 (l4media Redis ownership, DB 2)

**Шаг:** `L4D-REDIS-MEDIA-01`  
**Проект:** `l4media`  
**Ветка:** `l4desk/l4d-15c-media`  
**Output Handoff:** `H-L4D-REDIS-MEDIA-01-v1`  
**Registration:** `R-L4D-REDIS-MEDIA-01-v1`  
**Дата:** 2026-09-25

---

## 1. Scope

Перевод ownership media-сессий / routes / mountpoints из in-memory (`g_media_sessions`, dynamic `g_routes`) в **Redis DB 2**. Внешние lifecycle API не изменены. Hot RTP path (`g_clients`) остаётся in-memory.

Контроллер: `R-L4D-REDIS-IOT-01-v1` **не** покрывал l4media (`scope_project: iot-rpc-rest-app`). Введена отдельная регистрация `R-L4D-REDIS-MEDIA-01-v1` (§40).

## 2. Схема ключей (DB 2)

| Ключ | Value | TTL |
|---|---|---|
| `media:session:{session_id}` | `sid\|op\|sn\|dev\|mp\|rtp\|rtcp\|pin\|state\|ttl\|created\|started` | `ttl_sec+60` |
| `media:session-by-sn:{sn}` | `session_id` | как session |
| `media:route:{sn}` | `rtp:rtcp:mountpoint_id` | как session |
| `media:mountpoint:{id}` | `session_id` | как session |

- Пустой `pin` сериализуется как `-` (иначе `||` теряло поле).
- `REDIS_URL` не задан / Redis down → degrade in-memory.
- Signaling `media:webrtc:*` / `media:peer:*` — вне scope (следующий шаг).

## 3. Коммиты

| SHA | |
|---|---|
| `4979043fe11d16f4cdca7b5887c3750b748dd620` | feat ownership |
| `3748a73` | fix empty-pin serializer / restore parse |

## 4. Жизненный цикл

- **start / idempotent** → `media_redis_save_session` + `save_route`
- **stop** → `media_redis_drop_session`
- **bootstrap** (`media_redis_bootstrap`) → `media_redis_restore_sessions` → `g_media_sessions` + `upsert_route`
- **reconcile** → keep при `rtp_fresh` **или** `media_redis_owns_sn`

## 5. Верификация

| Проверка | Результат |
|---|---|
| C unit (`test_ingress_unit`) | **7/7 PASS** |
| Lifecycle suite (`test_media_lifecycle.py`) | **9/9 PASS** |
| Compile (alpine + hiredis) | OK, link `-lhiredis` |
| Redis keys после start | `media:session:*`, `media:route:*`, `media:mountpoint:*`, `media:session-by-sn:*` |
| **Restart-soak** | restored N sessions; health `state=active`; reconcile `orphans_cleaned=0`; RTP `live`; Janus mp `age_ms` непрерывный (без destroy/recreate) |

### Restart-soak (детально)

1. Сессия `sess-soak-ae92` + RTP → `fresh_rtp=true`.
2. `docker rm -f l4media-ingress` + `up -d`.
3. `[MEDIA REDIS] restored 3 session record(s)`.
4. Health: `state=active`, 404 устранён (после pin-fix).
5. Reconcile: `active_sessions=3`, cleaned mountpoints/routes **0**.
6. Новый RTP → `media_state=live`.
7. Janus mountpoint `9913` жив (`age_ms=57340`) — watcher-track не рвётся.

## 6. Инварианты

- Внешние REST / OpenAPI / lifecycle JSON — без изменений.
- Hot path RTP не пишет в Redis.
- TTL ownership = `ttl_sec+60` (не signaling 5 мин).
