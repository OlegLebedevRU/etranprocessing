# РљР°СЃРєР°Рґ РїСЂРѕРјРїС‚РѕРІ L4Desk

РљР°С‚Р°Р»РѕРі СЃРѕРґРµСЂР¶РёС‚ РёСЃРїРѕР»РЅСЏРµРјС‹Рµ РёР·РѕР»РёСЂРѕРІР°РЅРЅС‹Рµ РїСЂРѕРјРїС‚С‹ СЂРµР°Р»РёР·Р°С†РёРё MVP РёР· `..\l4desk-architecture.md`.

## РћР±СЏР·Р°С‚РµР»СЊРЅС‹Р№ РїРѕСЂСЏРґРѕРє Р·Р°РїСѓСЃРєР°

1. РџРµСЂРµРґР°С‚СЊ Р°РіРµРЅС‚Сѓ СЃРѕРґРµСЂР¶РёРјРѕРµ СЂРѕРІРЅРѕ РѕРґРЅРѕРіРѕ Р·Р°СЂРµРіРёСЃС‚СЂРёСЂРѕРІР°РЅРЅРѕРіРѕ С„Р°Р№Р»Р° РїСЂРѕРјРїС‚Р° РёР· С‚Р°Р±Р»РёС† РЅРёР¶Рµ.
2. РћР±РµСЃРїРµС‡РёС‚СЊ Р°РіРµРЅС‚Сѓ read-only РґРѕСЃС‚СѓРї Рє `PROMPT-STANDARD.md`, `contract-handoff.md`, СЂР°Р·СЂРµС€С‘РЅРЅС‹Рј СЂР°Р·РґРµР»Р°Рј Р°СЂС…РёС‚РµРєС‚СѓСЂС‹ Рё РєРѕРЅРµС‡РЅРѕРјСѓ РЅР°Р±РѕСЂСѓ data-only РІС…РѕРґРЅС‹С… Р°СЂС‚РµС„Р°РєС‚РѕРІ РїРѕ В§10 СЃС‚Р°РЅРґР°СЂС‚Р°.
3. РќРµ Р·Р°РїСѓСЃРєР°С‚СЊ РїСЂРѕРјРїС‚С‹ РїР°СЂР°Р»Р»РµР»СЊРЅРѕ Рё РЅРµ РїСЂРѕРїСѓСЃРєР°С‚СЊ СЃС‚СЂРѕРєРё СЃРїРёСЃРєР° РЅРёР¶Рµ.
4. РџРѕСЃР»Рµ РІС‹РїРѕР»РЅРµРЅРёСЏ runtime-РїСЂРѕРјРїС‚Р° РїСЂРѕРІРµСЂРёС‚СЊ Р»РѕРєР°Р»СЊРЅС‹Р№ РѕС‚С‡С‘С‚ Рё candidate handoff.
5. РљРѕРЅС‚СЂРѕР»Р»РµСЂ РґРѕСЃР»РѕРІРЅРѕ РґРѕР±Р°РІР»СЏРµС‚ РїСЂРѕРІРµСЂРµРЅРЅС‹Р№ candidate РІ `contract-handoff.md` С‚РѕР»СЊРєРѕ РїСЂРё РїРѕР»РЅРѕРј `ACCEPTED`.
6. Р›РёС€СЊ РїРѕСЃР»Рµ commit/push РѕР±С‰РµРіРѕ Р¶СѓСЂРЅР°Р»Р° Р·Р°РїСѓСЃРєР°РµС‚СЃСЏ СЃР»РµРґСѓСЋС‰РёР№ С„Р°Р№Р».
7. РџСЂРё Р»СЋР±РѕРј blocker СЃРѕР·РґР°С‘С‚СЃСЏ corrective prompt РїРѕ `CORRECTIVE-PROMPT-TEMPLATE.md`; РѕСЃРЅРѕРІРЅРѕР№ РєР°СЃРєР°Рґ РїСЂРёРѕСЃС‚Р°РЅРѕРІР»РµРЅ РґРѕ РЅРѕРІРѕРіРѕ РїСЂРёРЅСЏС‚РѕРіРѕ handoff.

РђРіРµРЅС‚ РЅРёРєРѕРіРґР° РЅРµ РІРѕСЃСЃС‚Р°РЅР°РІР»РёРІР°РµС‚ РѕС‚СЃСѓС‚СЃС‚РІСѓСЋС‰РёР№ РєРѕРЅС‚СЂР°РєС‚ РїРѕ РєРѕРґСѓ СЃРѕСЃРµРґРЅРµРіРѕ РїСЂРѕРµРєС‚Р°. Р•РґРёРЅСЃС‚РІРµРЅРЅС‹Р№ РєРѕРЅС‚СЂР°РєС‚РЅС‹Р№ input вЂ” С‚РѕС‡РЅС‹Р№ `ACCEPTED`-Р±Р»РѕРє РёР· `contract-handoff.md` Рё РїРµСЂРµС‡РёСЃР»РµРЅРЅС‹Рµ РІ РЅС‘Рј immutable artifacts СЃ digest.

## РџРѕСЃР»РµРґРѕРІР°С‚РµР»СЊРЅРѕСЃС‚СЊ

| в„– | Р¤Р°Р№Р» | Р•РґРёРЅСЃС‚РІРµРЅРЅС‹Р№ scope |
|---:|---|---|
| 1 | `L4D-00A-TOOLS.md` | `tools` |
| 2 | `L4D-00B-IOT.md` | `iot-rpc-rest-app` |
| 3 | `L4D-00C-PB.md` | `ProcessingBackend` |
| 4 | `L4D-00D-MEDIA.md` | `l4media` |
| 5 | `L4D-00E-MB.md` | `MenuBuilder` |
| 6 | `L4D-00F-SHARED.md` | `shared/etranprocessing_db` |
| 7 | `L4D-00G-DOCS.md` | `l4desk-service` |
| 8 | `L4D-01A-TOOLS.md` | `tools` |
| 9 | `L4D-01B-IOT.md` | `iot-rpc-rest-app` |
| 10 | `L4D-01C-DOCS.md` | `l4desk-service` |
| 11 | `L4D-02-IOT.md` | `iot-rpc-rest-app` |
| 12 | `L4D-03-MB.md` | `MenuBuilder` |
| 13 | `L4D-04A-SHARED.md` | `shared/etranprocessing_db` |
| 14 | `L4D-04B-PB.md` | `ProcessingBackend` |
| 15 | `L4D-04C-MB.md` | `MenuBuilder` |
| 16 | `L4D-05-MB.md` | `MenuBuilder` |
| 17 | `L4D-06A-PB.md` | `ProcessingBackend` |
| 18 | `L4D-06B-IOT.md` | `iot-rpc-rest-app` |
| 19 | `L4D-06C-MB.md` | `MenuBuilder` |
| 20 | `L4D-07-IOT.md` | `iot-rpc-rest-app` |
| 21 | `L4D-08A-MEDIA.md` | `l4media` |
| 22 | `L4D-08B-MB.md` | `MenuBuilder` |
| 23 | `L4D-09-MB.md` | `MenuBuilder` |
| 24 | `L4D-10-MB.md` | `MenuBuilder` |
| 25 | `L4D-11-MB.md` | `MenuBuilder` |
| 26 | `L4D-12-MB.md` | `MenuBuilder` |
| 27 | `L4D-13-MB.md` | `MenuBuilder` |
| 28 | `L4D-14-MB.md` | `MenuBuilder` |
| 29 | `L4D-15A-DOCS.md` | `l4desk-service` |
| 30 | `L4D-15B-IOT.md` | `iot-rpc-rest-app` |
| 31 | `L4D-15C-MEDIA.md` | `l4media` |
| 32 | `L4D-16-MB.md` | `MenuBuilder` |
| 33 | `L4D-17A-TOOLS.md` | `tools` |
| 34 | `L4D-17B-PB.md` | `ProcessingBackend` |
| 35 | `L4D-17C-IOT.md` | `iot-rpc-rest-app` |
| 36 | `L4D-17D-MEDIA.md` | `l4media` |
| 37 | `L4D-17E-MB.md` | `MenuBuilder` |
| 38 | `L4D-17F-DOCS.md` | `l4desk-service` |
| 39 | `L4D-18A-SHARED.md` | `shared/etranprocessing_db` |
| 40 | `L4D-18B-PB.md` | `ProcessingBackend` |
| 41 | `L4D-18C-IOT.md` | `iot-rpc-rest-app` |
| 42 | `L4D-18D-MEDIA.md` | `l4media` |
| 43 | `L4D-18E-MB.md` | `MenuBuilder` |
| 44 | `L4D-18F-DOCS.md` | `l4desk-service` |

## Р—Р°СЂРµРіРёСЃС‚СЂРёСЂРѕРІР°РЅРЅС‹Рµ РєРѕСЂСЂРµРєС‚РёСЂСѓСЋС‰РёРµ С€Р°РіРё

| Р—Р°Р±Р»РѕРєРёСЂРѕРІР°РЅРЅС‹Р№ С€Р°Рі | Corrective / С„Р°Р№Р» | Scope | Р РµРіРёСЃС‚СЂР°С†РёСЏ РІ Р¶СѓСЂРЅР°Р»Рµ | РџРѕСЃР»Рµ РІС‹РїРѕР»РЅРµРЅРёСЏ |
|---|---|---|---|---|
| `L4D-06B-IOT` | `L4D-06B-IOT-FIX-01` / [etran_dev-l4d-06b-iot-fix-01.md](etran_dev-l4d-06b-iot-fix-01.md) | `iot-rpc-rest-app` | `R-L4D-06B-IOT-FIX-01-v3` (v1, v2 РѕС‚РѕР·РІР°РЅС‹) | РџРѕРІС‚РѕСЂРЅР°СЏ РїСЂРёС‘РјРєР° `L4D-06B-IOT`; РЅРµ Р·Р°РїСѓСЃРє `L4D-06C-MB` |
| `L4D-08B-MB` | `L4D-08B-MB-FIX-01` / [L4D-08B-MB-FIX-01.md](L4D-08B-MB-FIX-01.md) | `MenuBuilder` | `R-L4D-08B-MB-FIX-01-v1` | РџРѕРІС‚РѕСЂРЅР°СЏ РїСЂРёС‘РјРєР° `L4D-08B-MB`; РЅРµ Р·Р°РїСѓСЃРє `L4D-09-MB` |
| `L4D-08B-MB` | `L4D-08B-FIX-01-MB` / [L4D-08B-FIX-01-MB.md](L4D-08B-FIX-01-MB.md) | `MenuBuilder` | `R-L4D-08B-FIX-01-MB-v1` | Р—Р°РјРµРЅР° media-flow `H-L4D-08B-MB-v1`; next `L4D-09-MB` |
| `L4D-08B-MB` | `L4D-08B-FIX-02-MB` / [L4D-08B-FIX-02-MB.md](L4D-08B-FIX-02-MB.md) | `MenuBuilder` | `R-L4D-08B-FIX-02-MB-v1` | РЈСЃС‚СЂР°РЅРµРЅРёРµ РєРѕР»Р»РёР·РёР№ session_id Рё Р·Р°РєСЂС‹С‚РёРµ СЂРёСЃРєР° С‚РµСЃС‚РѕРІ; next `L4D-14-MB` |
| `L4D-08B-FIX-02-MB` | `L4D-08B-FIX-03-MB` / [L4D-08B-FIX-03-MB.md](L4D-08B-FIX-03-MB.md) | `MenuBuilder` | `R-L4D-08B-FIX-03-MB-v1` | Р•РґРёРЅС‹Р№ durable stop-path РїРѕ IoT stop-РєРѕРЅС‚СЂР°РєС‚Сѓ `1.1.0`; next `L4D-14-MB` |
| `L4D-13-MB` | `L4D-13-MB-FIX-01` / [L4D-13-MB-FIX-01.md](L4D-13-MB-FIX-01.md) | `MenuBuilder` | `R-L4D-13-MB-FIX-01-v1` | Р—Р°РјРµРЅР° UX-С‡Р°СЃС‚Рё `H-L4D-13-MB-v1`; next `L4D-14-MB` |
| `L4D-17C-IOT` (РµС‰С‘ РЅРµ РЅР°С‡Р°С‚) | `L4D-17C-VIDEO-WATCH-IOT-01` / [L4D-17C-VIDEO-WATCH-IOT-01.md](L4D-17C-VIDEO-WATCH-IOT-01.md) | `iot-rpc-rest-app` | `R-L4D-17C-VIDEO-WATCH-IOT-01-v1` | Read-only video state observer РґР»СЏ РѕРґРЅРѕРіРѕ worker; РїРѕСЃР»Рµ provider acceptance РІРѕР·РѕР±РЅРѕРІРёС‚СЊ `L4D-17C-IOT` |
| `L4D-17C-IOT` | `L4D-17C-VIDEO-WATCH-MB` / [L4D-17C-VIDEO-WATCH-MB.md](L4D-17C-VIDEO-WATCH-MB.md) | `MenuBuilder` | `R-L4D-17C-VIDEO-WATCH-MB-v1` | Consumer video-watch polling для оператора; next `L4D-17E-MB` |
| `L4D-07-IOT` | `L4D-REDIS-IOT-01` / `iot-rpc-rest-app:docs/redis/prompt-stage2-redis-app.md` | `iot-rpc-rest-app` | `R-L4D-REDIS-IOT-01-v1` | Redis `LeaseRegistry`/`PresenceRegistry` (internal only); next `L4D-08A-MEDIA` |

Р РµРіРёСЃС‚СЂР°С†РёСЏ Рё Р°РґСЂРµСЃРЅС‹Р№ РґРѕРїСѓСЃРє РѕРїСЂРµРґРµР»СЏСЋС‚СЃСЏ В§8 `PROMPT-STANDARD.md` Рё РѕС‚РґРµР»СЊРЅРѕР№ append-only Р·Р°РїРёСЃСЊСЋ `CORRECTIVE_REGISTRATION` РІ Р¶СѓСЂРЅР°Р»Рµ. РџСЂРµР¶РЅРёРµ РїСЂРёРЅСЏС‚С‹Рµ Р·Р°РїРёСЃРё РЅРµ РјРµРЅСЏСЋС‚СЃСЏ. РџСЂРµРґРјРµС‚РЅС‹Р№ PIN-РІС…РѕРґ СЌС‚РѕРіРѕ FIX Р·Р°РјРµРЅС‘РЅ РєРѕРЅС‚СЂРѕР»Р»РµСЂРѕРј РЅР° `H-L4D-06A-PB-CONTRACT-01-v1` (СЃР°РјРѕСЃС‚РѕСЏС‚РµР»СЊРЅС‹Рµ РґРѕРєСѓРјРµРЅС‚С‹, DOCS_PUBLISHED); `H-L4D-06A-PB-v1` РѕСЃС‚Р°Р»СЃСЏ С‚РѕР»СЊРєРѕ sequence gate Р±РµР· С‡С‚РµРЅРёСЏ РёСЃС…РѕРґРЅРёРєРѕРІ. Р”Р»СЏ `H-L4D-02-IOT-v1` РѕС„РѕСЂРјР»РµРЅР° С‚РѕС‡РЅР°СЏ РїСЂРёРІСЏР·РєР° `B-L4D-02-IOT-GIT-v1` Рє РѕРїСѓР±Р»РёРєРѕРІР°РЅРЅС‹Рј Git-Р±Р°Р№С‚Р°Рј, РѕС‚Р»РёС‡Р°СЋС‰РёРјСЃСЏ РѕС‚ РёСЃС‚РѕСЂРёС‡РµСЃРєРёС… CRLF-РєРѕРїРёР№. Р”Р»СЏ Р°СЂС‚РµС„Р°РєС‚Р° `docs/l4desk/contracts/schemas/remote_session.schema.json` РєРѕРЅС‚СЂРѕР»Р»РµСЂРѕРј РЅРѕСЂРјР°С‚РёРІРЅРѕ СѓСЃС‚Р°РЅРѕРІР»РµРЅРѕ РїСЂР°РІРёР»Рѕ СЃРѕСЃС‚Р°РІРЅРѕРіРѕ РєРѕРЅС‚РµР№РЅРµСЂР° (В§10.6 СЃС‚Р°РЅРґР°СЂС‚Р°, СЂР°Р·РґРµР» 10 Р¶СѓСЂРЅР°Р»Р°): СЃС…РµРјС‹ РїРѕРґ `definitions` СЏРІР»СЏСЋС‚СЃСЏ СЃР°РјРѕСЃС‚РѕСЏС‚РµР»СЊРЅС‹РјРё РёР·РѕР»РёСЂРѕРІР°РЅРЅС‹РјРё РјРѕРґРµР»СЏРјРё, СЃСЃС‹Р»РєР° `#/$defs/RemoteSessionType` РІРЅСѓС‚СЂРё `RemoteSessionCreate` РІР°Р»РёРґРЅРѕ СЂР°Р·СЂРµС€Р°РµС‚СЃСЏ РІ РµС‘ Р»РѕРєР°Р»СЊРЅРѕРј `$defs`, РІР°Р»РёРґР°С†РёСЏ РІС‹РїРѕР»РЅСЏРµС‚СЃСЏ РїРѕ РјРѕРґРµР»СЏРј Р°РІС‚РѕРЅРѕРјРЅРѕ. Р”Р»СЏ СЌС‚РѕРіРѕ FIX СЃРѕРіР»Р°СЃРѕРІР°РЅ `DETACHED_V1`: РѕРєРѕРЅС‡Р°С‚РµР»СЊРЅС‹Р№ candidate РЅР°С…РѕРґРёС‚СЃСЏ РѕС‚РґРµР»СЊРЅРѕ РѕС‚ РѕС‚С‡С‘С‚Р° Рё СЃРѕРґРµСЂР¶РёС‚ SHA-256 Р·Р°С„РёРєСЃРёСЂРѕРІР°РЅРЅС‹С… Р±Р°Р№С‚РѕРІ РѕС‚С‡С‘С‚Р° (В§9 СЃС‚Р°РЅРґР°СЂС‚Р°).

Р”РѕРєСѓРјРµРЅС‚Р°С†РёРѕРЅРЅС‹Р№ provider/corrective С€Р°Рі `L4D-06A-PB-CONTRACT-01` РІС‹РїРѕР»РЅРµРЅ РєРѕРЅС‚СЂРѕР»Р»РµСЂРѕРј РїРѕ РѕС‚РґРµР»СЊРЅРѕРјСѓ РїРѕСЂСѓС‡РµРЅРёСЋ РїРѕР»СЊР·РѕРІР°С‚РµР»СЏ; [РѕС‚С‡С‘С‚](contracts/certificate-pin-v1/verification.md), [РєРѕРЅС‚СЂР°РєС‚](contracts/certificate-pin-v1/contract.md), [СЃС…РµРјС‹](contracts/certificate-pin-v1/schemas.json), [РїСЂРёРјРµСЂС‹](contracts/certificate-pin-v1/examples.json) РѕРїСѓР±Р»РёРєРѕРІР°РЅС‹ РІ `1971e51f1e7764a31d586174e42513160f8598eb`. Runtime РєРѕРґ/РёРЅС„СЂР°СЃС‚СЂСѓРєС‚СѓСЂР° РЅРµ РёР·РјРµРЅСЏР»РёСЃСЊ; СЌС‚Рѕ РЅРµ РїСЂРёС‘РјРєР° 06B.

Р—Р°РїРёСЃСЊ `AUTHORIZED` СЂР°Р·СЂРµС€Р°РµС‚ С‚РѕР»СЊРєРѕ СѓРєР°Р·Р°РЅРЅС‹Р№ corrective scope Рё РЅРµ СЏРІР»СЏРµС‚СЃСЏ `ACCEPTED` runtime-handoff. РќР°Р»РёС‡РёРµ Р·Р°РїРёСЃРё РІ СЂР°Р±РѕС‡РµРј РґРµСЂРµРІРµ РЅРµ РґРѕРєР°Р·С‹РІР°РµС‚ РїСѓР±Р»РёРєР°С†РёСЋ: РїРµСЂРµРґ Р·Р°РїСѓСЃРєРѕРј РґРѕР»Р¶РЅС‹ Р±С‹С‚СЊ Р·Р°РєРѕРјРјРёС‡РµРЅС‹ Рё РѕС‚РїСЂР°РІР»РµРЅС‹ РІ remote РїСЂРѕРјРїС‚, СЂРµРіРёСЃС‚СЂР°С†РёСЏ Рё СЃРѕРіР»Р°СЃРѕРІР°РЅРЅС‹Рµ РїСЂР°РІРёР»Р°. РџСЂРѕРІРµСЂРёС‚СЊ РѕРїСѓР±Р»РёРєРѕРІР°РЅРЅС‹Р№ commit РїР°РєРµС‚Р°. Р”Рѕ РїСЂРёРЅСЏС‚РёСЏ СЂРµР·СѓР»СЊС‚Р°С‚Р° РёСЃС…РѕРґРЅРѕРіРѕ `L4D-06B-IOT` РѕСЃРЅРѕРІРЅРѕР№ РєР°СЃРєР°Рґ РѕСЃС‚Р°С‘С‚СЃСЏ РѕСЃС‚Р°РЅРѕРІР»РµРЅРЅС‹Рј.

Р”Р»СЏ РІРѕР·РѕР±РЅРѕРІР»РµРЅРёСЏ РїРµСЂРµРґР°С‚СЊ Р°РіРµРЅС‚Сѓ С‚РѕС‡РЅС‹Р№ С„Р°Р№Р» FIX РёР· **РЅРѕРІРѕРіРѕ** commit D Рё СЃРѕРѕР±С‰РµРЅРёРµ РєРѕРЅС‚СЂРѕР»Р»РµСЂР° СЃ РїРѕР»РЅС‹Рј SHA D/РїРѕРґС‚РІРµСЂР¶РґРµРЅРёРµРј push РІ `origin/l4desk/l4d-06a-pb`. Р’ СЃРѕРѕР±С‰РµРЅРёРё СѓРєР°Р·Р°С‚СЊ: РІС‹РїРѕР»РЅРёС‚СЊ РѕР±РЅРѕРІР»С‘РЅРЅС‹Р№ FIX РїРѕ СЃС‚Р°РЅРґР°СЂС‚Сѓ 1.2.0 (В§10.6), СЂРµРіРёСЃС‚СЂР°С†РёРё v3, С‡РµС‚С‹СЂС‘Рј СЂР°Р·СЂРµС€С‘РЅРЅС‹Рј PIN-РґРѕРєСѓРјРµРЅС‚Р°Рј, IOT byte binding Рё РЅРѕСЂРјР°С‚РёРІРЅРѕРјСѓ РїСЂР°РІРёР»Сѓ СЂР°Р·СЂРµС€РµРЅРёСЏ СЃС…РµРј RemoteSession; РЅРµ С‡РёС‚Р°С‚СЊ СЃРµРјСЊ .py СЃС‚Р°СЂРѕРіРѕ 06A; РІРµСЂРЅСѓС‚СЊ РѕС‚С‡С‘С‚ Рё detached candidate РґР»СЏ РїРѕРІС‚РѕСЂРЅРѕР№ РїСЂРёС‘РјРєРё. `b2664467...` РЅРµ Р·Р°РјРµРЅСЏРµС‚ РїСѓР±Р»РёРєР°С†РёСЋ D. SHA D РїРµСЂРµРґР°С‘С‚СЃСЏ РѕС‚РґРµР»СЊРЅРѕ РїРѕСЃР»Рµ commit, С‡С‚РѕР±С‹ РЅРµ СЃРѕР·РґР°РІР°С‚СЊ СЃР°РјРѕСЃСЃС‹Р»РєСѓ РІ СЃР°РјРѕРј Р·Р°РґР°РЅРёРё.

## Acceptance РєРѕРЅС‚СЂРѕР»Р»РµСЂР°

РџРµСЂРµРґ РґРѕР±Р°РІР»РµРЅРёРµРј handoff РєРѕРЅС‚СЂРѕР»Р»РµСЂ РїСЂРѕРІРµСЂСЏРµС‚:

- scope РЅРµ РІС‹С€РµР» Р·Р° РѕРґРёРЅ РїСЂРѕРµРєС‚;
- РѕС‚СЃСѓС‚СЃС‚РІСѓСЋС‚ РЅРµР·Р°СЏРІР»РµРЅРЅС‹Рµ РёР·РјРµРЅРµРЅРёСЏ;
- РѕР±СЏР·Р°С‚РµР»СЊРЅС‹Рµ С‚РµСЃС‚С‹ Р·РµР»С‘РЅС‹Рµ;
- commit СЃСѓС‰РµСЃС‚РІСѓРµС‚ РІ СѓРєР°Р·Р°РЅРЅРѕР№ remote-РІРµС‚РєРµ;
- deploy/publish Рё smoke РїРѕРґС‚РІРµСЂР¶РґРµРЅС‹;
- artifact paths РґРѕСЃС‚СѓРїРЅС‹ Рё РёС… SHA-256 СЃРѕРІРїР°РґР°РµС‚;
- contract payload РїРѕР»РѕРЅ Рё РЅРµ СЃРѕРґРµСЂР¶РёС‚ РїСЂРµРґРїРѕР»РѕР¶РµРЅРёР№;
- `consumers` Рё `next_prompt_id` СЃРѕРІРїР°РґР°СЋС‚ СЃ РїРѕСЃР»РµРґРѕРІР°С‚РµР»СЊРЅРѕСЃС‚СЊСЋ;
- РІ Р¶СѓСЂРЅР°Р»Рµ РЅРµС‚ РєРѕРЅС„Р»РёРєС‚СѓСЋС‰РµРіРѕ handoff.

РЎР°Рј С„Р°РєС‚ РѕС‚РІРµС‚Р° Р°РіРµРЅС‚Р° В«РіРѕС‚РѕРІРѕВ» РЅРµ СЏРІР»СЏРµС‚СЃСЏ acceptance. Р”Р»СЏ Р·Р°РїСѓСЃРєР° РѕС‚РґРµР»СЊРЅРѕРіРѕ Р°РіРµРЅС‚Р°-РєРѕРЅС‚СЂРѕР»Р»РµСЂР° С„РёРєСЃР°С†РёРё handoff РёСЃРїРѕР»СЊР·СѓРµС‚СЃСЏ РїСЂРѕРјРїС‚ `HANDOFF-CONTROLLER-PROMPT.md`.


## L4D-17E FIX and 17F handoff readiness

`H-L4D-17E-MB-FIX-01-v1` is accepted in `contract-handoff.md` from the
published v3 [report](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-FIX-01-v3-report.md)
and [detached candidate](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-FIX-01-v3-candidate.md).
Earlier 17E reports and registrations remain historical evidence.

The original `L4D-17F-DOCS.md` stays closed because four historical inputs
have no artifact digests and it names the unaccepted original 17E output.
Use the published data-only 17A–C, 17D media and 17E exports and the
addressed [L4D-17F-DOCS-FIX-01.md](L4D-17F-DOCS-FIX-01.md) after its
registration is published. The exports do not replace current black-box
runtime verification.

### 17F evidence collected on 2026-09-27

The historical 17F verdict was `BLOCKED_TESTS`; these reports preserve
the evidence collected before the final v2 packet:

- [Grace, forced stop and recovery](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17F-MB-GRACE-TESTPATH-01-report.md).
- [Short archive/restore fixture](../handoffs/L4D-17F-MEDIA-ARCHIVE-EVIDENCE-01-report.md).
- [Archive consumer contract](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17F-MB-ARCHIVE-CONSUMER-EVIDENCE-01-report.md).
- [Payment recovery and Hub correlation](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17F-MB-PAYMENT-RECOVERY-01-report.md).
- [Current deployment matrix and remaining gaps](../handoffs/L4D-17F-DEPLOY-EVIDENCE-01-report.md).

The user excludes real three-year retention waiting from technical
acceptance and authorizes mock payment evidence until the release.

### Final 17F packet

Use [the registered v2 prompt](L4D-17F-DOCS-FIX-01-v2.md), which preserves
the original reports and consumes four accepted data-only inputs.

- [Final report](../handoffs/L4D-17F-DOCS-FIX-01-v2-report.md).
- [Detached candidate](../handoffs/L4D-17F-DOCS-FIX-01-v2-candidate.md).
- [Git input verification](../handoffs/evidence/17f-deploy-20260927/final-contract-gate.json).
- [Media build provenance and TTL equivalence](../handoffs/L4D-17F-MEDIA-PROVENANCE-02-report.md).

Agent `1.8.2-beta-1` is the owner's approved release baseline, unchanged.
The producer verdict is complete; independent acceptance is recorded only
in `contract-handoff.md`. The report/candidate do not enable production
commercial flags. Before running 18A, its historical required input
`H-L4D-17F-DOCS-v1` must be addressed to the accepted corrective output
by the controller; publishing this packet alone does not open that gate.


## 18A shared corrective release registration

The historical `L4D-18A-SHARED.md` has an absent 17F handoff ID. Use
[`L4D-18A-SHARED-FIX-01.md`](L4D-18A-SHARED-FIX-01.md) only after
`R-L4D-18A-SHARED-FIX-01-v1` is published in `contract-handoff.md`.
Its exact inputs are accepted `H-L4D-17F-DOCS-FIX-01-v1` and
`H-L4D-04A-SHARED-v1`; output acceptance remains independent.

## 18B ProcessingBackend corrective rollout registration

The historical 18B inputs include missing and mismatched artifact digests.
Use [`L4D-18B-PB-FIX-01.md`](L4D-18B-PB-FIX-01.md) after published
`R-L4D-18B-PB-FIX-01-v1`. Direct inputs are accepted 18A shared and
`H-L4D-18B-PB-EVIDENCE-CONTRACT-01-v1`; the latter is a finite data-only
export of historical PB reports and the accepted PIN contract. The
registration does not accept deployment or open 18C.

## 18C IoT corrective gate

The historical 18C direct inputs have missing or mismatched Git/raw digests. Published `R-L4D-18C-IOT-FIX-01-v1` registers [`L4D-18C-IOT-FIX-01.md`](L4D-18C-IOT-FIX-01.md) with accepted 18B and a finite data-only IoT evidence export as direct inputs. The export does not prove current runtime. Before build/deploy, the IoT owner must harden the build context against private configuration and verify release readiness. 18C acceptance and 18D remain pending.

## 18D media corrective gate

The historical 18D subject handoffs have missing or mismatched digest and addressing, and 18C includes neighboring implementation artifacts. Published `R-L4D-18D-MEDIA-FIX-01-v1` registers [`L4D-18D-MEDIA-FIX-01.md`](L4D-18D-MEDIA-FIX-01.md) with two finite data-only direct inputs and accepted 18C as sequence-only gate. The registered scope is the clean `etranprocessing-l4d-18d-media` worktree. Publication permits the runtime owner to run the full media release gate; 18D acceptance and 18E remain pending.

## 18D media credential consumer rotation

User-approved separate MenuBuilder owner step [`L4D-18D-AUTH-MB-FIX-01.md`](L4D-18D-AUTH-MB-FIX-01.md) is registered as `R-L4D-18D-AUTH-MB-FIX-01-v1`. Its only direct input is the finite media auth data-only export. It changes only confirmed MB private consumers in coordination with the media owner; it does not accept 18D or open 18E.

## 18F release documentation corrective gate

The original [`L4D-18F-DOCS.md`](L4D-18F-DOCS.md) is blocked because the
accepted 18A–18D handoffs do not address it. The accepted 18E controller
handoff has been restored to `main` without editing its original block.
User-authorized `R-L4D-18F-DOCS-FIX-01-v1` registers
[`L4D-18F-DOCS-FIX-01.md`](L4D-18F-DOCS-FIX-01.md) with exact access to five
accepted inputs and finite report/evidence reads. This registration permits
the docs-only release audit; it does not accept 18F or close the cascade.
