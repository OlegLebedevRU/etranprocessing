# l4capture CHANGELOG

## [Unreleased]

### Fixed

- **False `HIGH_LOAD` (exit 99) при низком CPU / sparse desktop.** Missed pacing
  slots засчитывались как raw-drop. Latest-frame desktop (DXGI `NO_FRAME`,
  2–10 fps апдейтов) — не непрерывный источник на target fps: медленный
  P-frame/IDR ≠ потеря кадра. Окно 4 pass + 8 phantom-drop = 66% → D0 →
  5 bad-окон → `STOP_HIGH_LOAD`. Missed pacing больше не даёт raw-drop.
  Fail-closed сохранён: p95 при n≥20, настоящие convert/acquire/enc/tr drops,
  порог 20% и лестница D0→D3→STOP без изменений.
- **`frame.pts_ms` читался после `release_frame`.** PTS снимается до Unmap.
- **Broadcast delay рос и застывал на ~2–3 с (vs ffmpeg).** PTS был синтетическим
  (`frames_captured * 1000/fps`) и перезаписывал wall-clock метку захвата.
  При расхождении effective fps и claimed fps RTP media clock уходил от wall,
  jitter buffer WebRTC/Janus растягивал задержку до потолка. Теперь
  session-relative wall-clock PTS (`l4c_pts_session_ms`) от capture-момента;
  idle gap сохраняется как timestamp hole, не как фантовый поток кадров.

- **Multi-desktop: DXGI возвращал весь монитор вместо ROI.** При 2+ дисплеях
  Desktop Duplication всегда отдаёт полный output; scale/encode брали
  `frame.width/height` = весь монитор. Теперь `source_rect` режется в
  output-local координатах (crop pointer), в поток идёт только выбранный
  регион. GDI и так делал BitBlt по rect.
- **Missed pacing считал ожидание `AcquireNextFrame`.** `frame_t0` ставился
  до acquire (timeout до 50 мс на idle-десктопе / 2-мониторной схеме).
  Ожидание кадра давало фантомные raw-drop → лестница → `HIGH_LOAD`.
  Теперь `frame_t0` после успешного acquire — в бюджет попадает только
  scale+convert+encode+send.
- **False `HIGH_LOAD` (exit 99) on idle/low-load desktop.** DXGI `AcquireNextFrame`
  timeout (`L4C_ERR_NO_FRAME` — экран не менялся) считался `raw.dropped`. На
  статичном desktop почти каждый кадр — timeout, drop-rate ~100% → лестница
  D0→D2→D3→`STOP_HIGH_LOAD` при 2–3% CPU. Теперь `NO_FRAME` не drop (latest-frame).
- **Encoder `NO_FRAME` (rate-control skip / MFT no-output) считался `enc.dropped`.**
  Skip — штатное решение кодера, не отказ. Теперь только `frames_skipped++`.
- **`processing_p95_ms` копился как max, а не p95.** Один медленный IDR
  (>100 мс при 10 fps) помечал всё окно bad. Теперь семплы копятся в окно,
  p95 считается через `l4c_degrade_p95_from_samples` при закрытии.
- **p95-критерий при малой выборке = max.** При `processing_samples < 20`
  (`L4C_DEGRADE_P95_MIN_SAMPLES`) p95-критерий не применяется: редкие апдейты
  экрана ≠ перегрузка.
- **`ACT STOP_HIGH_LOAD` логировал уже обнулённый `win_acc`** (после `memset`
  при закрытии окна) — в логе всегда `raw=0/0 … n=0`. Теперь логируется реальное
  окно, по которому принято решение.
- **Overload accounting (missed pacing slots → raw drops) выполнялся и без
  реального кадра** — фантомные drops на idle/reconfigure. Теперь только после
  успешного capture (`frame_attempted`).

### Changed

- **MFT Intel QSV: рабочий encode + adaptive negotiate + persistent cache.**
  Корневые причины `supported=false` и `enc=1`:
  1) fail-closed probe 2.0 с обрывал `ActivateObject` (~1–4 с у QSV);
  2) type negotiation падал `MF_E_INVALIDMEDIATYPE` без `mfuuid` GUID major/subtype
     из `GetOutputAvailableType`;
  3) async MFT без `IMFMediaEventGenerator` (`METransformNeedInput`/`HaveOutput`)
     давал `MF_E_NOTACCEPTING`;
  4) `STREAM_CHANGE` без пересоздания output sample → `E_UNEXPECTED`;
  5) `out_au->nals` не инициализировался (pointer) → crash при разборе AU.
  Теперь: cache-first (`mft_capability.ini`), discovery probe ≤5 с только при
  miss/retryable, стратегии negotiate (avail GUID / crafted / min), `strategy=`
  в кэше, OpenH264 fallback сохранён. Тесты 121/121, encode smoke 5/5 IDR.
- **MFT: persistent capability cache вместо пробы на каждый старт.** Результат
  probe сохраняется в `mft_capability.ini` рядом с exe (`hw=0|1`, `reason`).
  Cache hit (`ok` / `unavailable`) — мгновенно, без `MFTEnumEx`/`ActivateObject`.
  Probe только при cache miss / retryable (`timeout`, `negotiate_fail`,
  `runtime_fail`) — редкий, бюджет **5.0 с** (fail-closed: `true` после бюджета
  не возвращается). Runtime-отказ MFT (`init` fail / `DEVICE_LOST`) помечает
  кэш retryable; штатный fallback OpenH264 сохранён. Probe перебирает все HW
  MFT, не только `ppActivate[0]`.
- **OpenH264: `bEnableBackgroundDetection` / `bEnableAdaptiveQuant` → 0.**
  Меньше CPU на кадр (encode p95 на стенде рос 16→250 мс).
- **RTP `SO_SNDBUF` 4 MiB → 64 KiB.** Live video: ограниченная очередь —
  при TCP HOL upstream stale-кадры не должны копиться секундами.
- **`WSAEWOULDBLOCK` больше не ретраится `50×Sleep(1)` на пакет** (до 50 мс
  на пакет, IDR AU — секунды). Политика: drop AU + force IDR
  (`L4C_RTP_SEND_MAX_WOULDBLOCK_RETRIES = 0`), fail-closed счётчики сохранены.
- **MFT probe flags:** `0x100` → `0x4` (`MFT_ENUM_FLAG_HARDWARE` по `mfapi.h`;
  `0x100` — не флаг). В `mf_init` — `HARDWARE|SORTANDFILTER` (`0x4|0x40`) для
  стабильного выбора MFT. В probe без `SORTANDFILTER`: полный скан+сортировка
  дают >2 с на стенде без HW MFT.
- **MFT probe raster:** `854x480` → `640x480` (16-aligned, быстрая type-
  negotiation). Probe проверяет только NV12→H264 capability, не реальный растр.
- **MFT probe budget:** ранний bailout до дорогих `Set*Type` при `>1.5s`;
  контракт теста — fail-closed (не вернуть `true` после 2.0s) и абсолютный
  потолок от зависания.
