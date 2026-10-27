# Desktop raster adaptation — текущий handoff, 27.10.2026

## Состояние и владельцы

- Цель: при перегрузке сохранять свежую картинку, допуская пропуски и временное снижение качества; единственный runtime-контроллер качества — первичный инициатор сессии.
- Native: ветка `perf/l4capture-desktop-pacing`, commit `eb10421cbc66b48929666908f63ca67748787ea5`, [draft PR11](https://github.com/OlegLebedevRU/l4tools/pull/11). Код опубликован после явного разрешения оператора. Подпись, упаковка и выпуск бинарников отложены.
- Native worktree: `D:\repo\platerra\Public\etranprocessing\.worktrees\l4capture-desktop-pacing`; основной репозиторий tools: `D:\repo\platerra\Public\l4tools`.
- MenuBuilder: [PR136](https://github.com/OlegLebedevRU/etranprocessing/pull/136), code commit `b6651368fb8692c864a59e5b8f6888094340c0c0`, ветка `fix/desktop-receiver-queue` от main `6a6a9832ac8055a9969ec31a653bdc775c3549de`; изменены только политика frontend, её тест и документация. Backend/app1 в этой итерации не менялись.
- Producer/consumer: исходный браузер -> MenuBuilder -> app1 -> l4desk -> l4capture; RTP через leo4proxy/Janus -> браузер. Proxy сообщает измерения и не выбирает качество. Writer key/org/user/lease/epoch/generation, STOP/expiry и final-send gate сохранены. Начальный снимок геометрии ввода не заменяется live-размером кодирования.
- Старые H264-агенты не обязаны выполнять optional runtime/raster-контракт. Совместимость старых IPC-layout проверена по коду; живой старый бинарник недоступен по согласованию.

## Установлено на773

- `l4capture`: SHA256 `D8DE87FE781BB9738873AFF9D40116EB865C30E9889FEC662523F529E2E73E29`.
- `l4desk`: `AA0B644AAD511FCC5E9BAF7FACDD81F44B6F7DDBA33AE9D8B3ABFF9EB3DC43DC`.
- `leo4proxy`: `DA00DBBB318DA611349E46FAC1E685A4F02EFC88106D5736A6711ED09E6C27AB`.
- Последняя установка изменила только capture, службы не перезапускались. Original backups и предыдущий кандидат сохранены в manifest/controller. Отката после тестов нет.
- Административный контроллер `D:\repo\platerra\Public\etranprocessing\capture-latency-control.ps1` оставлен для согласованных фиксированных операций; произвольные команды не принимает.

## Изменения

- Ограниченный pipeline: неизменяемый обрабатываемый raw + один заменяемый свежий raw; encoder worker, без очереди устаревших кадров.
- MFT NeedInput/HaveOutput обслуживаются независимо, PTS читается из реального output sample. Busy rate-operation повторяется без ложного завершения. HEVC использует существующий внешний watchdog; реальные ошибки HRESULT не подавляются.
- Optional loopback L4QT send-clock отличает очередь транспорта от времени кодирования и удаляется proxy до WAN. Старый producer сохраняет прежний путь; исправлено распознавание H264 STAP-A при repair.
- Exact separable area scaler хранит переиспользуемые строки; scalar/SSE2 дают побайтовый результат эталонного area. Нет per-frame allocation или нового требования к железу. Между capture/scale обслуживаются готовый AU и IPC/STOP.
- OpenH264 обновляет global и spatial-layer0 max bitrate; восстановление выше исходного layer ceiling больше не отклоняется.
- Инициатор раньше уменьшает растр при измеренном большом IDR, готовит бюджет следующего большего растра до пробы и по решению оператора ограничивает cooldown неудачных проб30секундами. Увеличение bitrate остаётся постепенным, поэтому полный возврат качества занимает дольше30секунд.

## Проверено

- [x] Штатная сборка capture x86/x64/default, без vendor rebuild и подписи.
- [x] x86/x64: focused live/MFT-gate tests, RTP/feedback cases; после scaler/OpenH264 изменений по10 scaler cases и1 actual-encoder recovery case на каждой архитектуре. Всего174 теста зарегистрировано, полный набор174 не заявляется выполненным.
- [x] Frontend:19 policy tests, включая1000-step repeated-failure replay; `npm run build`. Проверенный asset `/assets/index-Ce50-6sy.js` действительно загружен в Chrome.
- [x] Chrome155.0.8059.85, desktop773, источники DISPLAY1(3000x2000) и DISPLAY2(1920x1080),10FPS source, оба кодека; A/B/A по90секунд. Два отдельных10-минутных HEVC-прогона:60с A1 /240с B1Mbps /300с A2.
- [x] STOP при отправленной raster-команде:1261мс до UI/server stopped, capture process отсутствует. Это подтверждает отмену pending-команды, а не измерение конкретной внутренней стадии hardware-init.
- [x] Scoped secret scans и diff-check без находок. Неизменённые Python/backend/certificate проверки не запускались повторно.

| Прогон, участок B1Mbps | decoded FPS | Mbps | Доп. задержка median/p95/max, мс | RTP loss / decoder drops |
|---|---:|---:|---|---|
| H264 DISPLAY1, `raster11-soft` |3.56|0.286|84.8 /350.6 /1240.8|0 /0|
| H265 DISPLAY2, `raster12-hevc` |8.24|0.304|117.8 /506.7 /3470.8|0 /0|
| H264 DISPLAY2, `raster13-soft` |7.86|0.471|200.9 /245.4 /612.3|0 /0|
| H265 DISPLAY1, final `raster17-retry30`,240с |8.43|0.263|337.5 /479.4 /1946.5|0 /0|

Задержка — рост смещения rVFC/RTP-clock относительно начала A1, не абсолютное capture-to-display. Video-element drops/freezes фиксируются отдельно и не названы сетевыми потерями. В final B:107 video drops/25 freezes; A2:42/16. В final A2:8.05FPS/1.210Mbps, lag106.8/212.9/1500.6мс; full3000x2000 впервые восстановлен на76.1с, повторно172.9с. В предыдущем uncapped-backoff прогоне full вернулся235.1с. Начальные quality-state и фоновая нагрузка различались: это функциональное evidence восстановления, не строгий процентный A/B-бенчмарк.

## Native trace и ресурсы

- Отдельная20-секундная `raster15-trace`:187 decoded/enqueued,184 selected;184 rVFC frames сопоставлены по media PTS и RTP timestamp,3 внутренних кадра не выбраны композитором. RTP-loss/decoder-drop delta0.
- Compositor median16.658/p9517.669мс;1112/1135 интервалов около16.7мс, ни одного около50мс. Decode->callback4.056/7.334мс, callback->enqueue0.289/0.652мс, enqueue->select8.197/16.437мс. rVFC receive->expectedDisplay135.3/215.1мс. Компоненты не складываются в неподтверждённую абсолютную задержку.
- Trace transfer/collection нарушает плавность;401 callbacks за весь diagnostic включают время вне native trace. Выводы используют184 matched frames. `raster16-retry30` намеренно остановлен и не засчитан: начало совпало с разбором трассы. Финальный `raster17` выполнялся без сборок/разбора трасс, с trace и visual-clock OFF.
- CPU в final A1 median100%, B79%, A286%; фоновые приложения не завершались. GPU A2 peaks3D93%, VideoDecode74%, VideoProcessing72%. WMI VideoEncode0 не доказывает software fallback: native backend2 был активен.
- Software DISPLAY1 capture private43–74MiB. HEVC quarter134–219MiB, half примерно204–248MiB, full обычно327–364MiB; transition peak489.6MiB. После уменьшения возвращается примерно206MiB; это не обещание общего process-memory ceiling или доказательство для всех драйверов.

## Что осталось / выпуск

- [x] Опубликовать frontend follow-up PR136.
- [ ] Слить PR136 и адресно развернуть только MenuBuilder frontend; предыдущие PR134/PR156 уже deployed, backend/app1 повторно не нужны. До этого браузерные результаты относятся к локально поданному candidate dist при реальных production API/media.
- [ ] Финальный native signing/packaging/publication — отдельный согласованный этап.
- [ ] Старый агент live E2E недоступен; реальные слабые uplink источника/другая аппаратура не проверены. Проверялось ограничение browser downlink1Mbps, без гарантии для любых слабых каналов.
- Ограничения: начальный крупный IDR может дать секундный пик; software H264 на DISPLAY1 ограничен CPU; более частые пробы30с означают согласованные краткие пропуски. Пустой/статичный экран не считается доказательством запаса канала.

## Evidence и cleanup

- Прогоны/trace: `D:\repo\platerra\Public\etranprocessing\.worktrees\desktop-video-observability\MenuBuilder\frontend\.pytest_tmp\desktop-performance-native` (`raster11`..`raster17`, `raster15-native-trace-summary.json`, `stop-during-raster-result.json`).
- Native tests/build/host: native `.scratch/capture-pacing/` (`test-scale-final-*`, `test-mft-progress-*`, `build-scale-final.log`, `host-summary.jsonl`). Local patch: `desktop-latency-eb10421.patch`.
- Трансляция штатно остановлена, process capture отсутствует; network emulation снята; test-overlay и timers остановлены; source-window свёрнуто, Chrome/пользовательские вкладки сохранены. Host sampler завершён. Тестовая вкладка пока хранит candidate JS до frontend rollout; после rollout перезагрузить без локальных routes.
- Readiness MCP Ops: UNAVAILABLE (tools отсутствуют); SSH остаётся штатным разрешённым транспортом. Секреты, writer keys, PIN/JWT в evidence/PR не публикуются.

## История итераций ниже — не текущий статус

## Installed software follow-up and browser gates (2026-10-27)

- Capture candidate `D8DE87FE781BB9738873AFF9D40116EB865C30E9889FEC662523F529E2E73E29` installed via controller request073cb7c6837d495b9283f10ab853a4a8; capture only, no services restarted. Previous880D retained in manifest. Unsigned/private, not published.
- Final scaler10 cases plus actual OpenH264 recovery1 case passed on each x86/x64; native build all passed. Source scanner18 files/0 findings. Logs native `.scratch/capture-pacing/test-scale-final-x64.log`, `test-scale-final-x86.log`, `build-scale-final.log`.
- `raster11-soft` DISPLAY1 modern GDI/OpenH264, local candidate frontend, 90s A/B/A completed. A1:3.65FPS/.389Mbps; B1Mbps:3.56FPS/.286Mbps, additional RTP-clock lag median84.8ms/p95350.6ms/max1240.8ms; A2:3.35FPS/.384Mbps. Each phase0 RTP loss/0 decoder drops. CPU about92%; full-raster recovery is accepted but CPU delay causes subsequent half-raster adaptation. Video element drops/freezes remain separately recorded (B5/3). Do not call these network losses.
- `raster12-hevc` DISPLAY2 90s A/B/A completed. A1:9.66FPS/1.255Mbps; B1Mbps:8.24FPS/.304Mbps, lag median117.8ms/p95506.7ms/max3470.8ms; A2:9.46FPS/.442Mbps, median-4ms/p95232.2ms/max308.8ms. All phases0 RTP loss/0 decoder drops; B video drops89/freezes24. Initial large IDR peak remains. A2 bitrate restored2000 by81.4s, raster still960x540 at88.6s; full-raster restoration is not proven by this truncated90s window.
- Browser diagnostic: Playwright CDP connect was blocked by an unrelated unresponsive cameras tab. Test-only preload `cdp-owned-tabs-only.cjs` excludes its exact target from attachment; tab remains open, no browser-library/product modification. Desktop viewer/source respond normally. Both gates use trace/visual clock OFF and local candidate dist; deployed static frontend remains prior PR134 until separate release.
- DISPLAY2 H.264 and10-minute DISPLAY1 HEVC soak still pending. No claim of all-hardware performance or absolute capture-to-display latency: lag above is growth relative to A1 source RTP clock.

## Resume evidence 2026-10-27 (before software follow-up acceptance)

- Controller restarted by operator; capture `880D9500C22CF632E338DDE84454FF3A010864B79EE15E39AAFFD91AAB68A506` installed successfully, no service restart. Prior pause hashes below are historical.
- `raster9-budget`, DISPLAY1 HEVC, Chrome155.0.8059.85, local candidate frontend: A/B/A 90 seconds each passed. B at1Mbps:8.84 decoded FPS,0.398Mbps,0 RTP packets lost,0 decoder drops; additional RTP-clock lag median240.6ms,p95454ms,initial peak5729.6ms. A2 restored full3000x2000/2000kbps by89.2s; no encoder hang.
- `raster10-soft`, DISPLAY1 GDI/OpenH264: intentionally STOPPED as diagnostic. CPU scaling limited unlimited-network output to2-3FPS; same-raster bitrate recovery at1003kbps refused after853kbps. Not an acceptance pass.
- Software follow-up under test: reusable exact separable area workspace with scalar/SSE2 paths, no per-frame allocation; service completed AU/IPC between capture and scale stages; update OpenH264 ALL and spatial-layer0 maximum bitrate together. Source API confirms ALL alone leaves the layer initialization ceiling unchanged.
- Native source18 changed files secret scan0 findings; no signing/publication. Software follow-up browser gates and final10-minute soak still pending.

# Драфт handoff: изменение растра desktop по каналу инициатора

## Возобновлено по команде пользователя — 2026-10-27

Пауза ниже снята явным «продолжи». Scope прежний: unsigned debugging на773,
первичный инициатор — единственный writer, STOP/lease/финальный send gate
сохранены; подпись и native publication по-прежнему отложены.
MCP Ops инструменты не доступны; `[MCP Ops Readiness: UNAVAILABLE]`, SSH fallback.
После паузы readonly SSH подтвердил прежние app1/backend digests: d27a4cf/760315c;
новых server изменений на этом этапе нет.

- Исправлен async-MFT flow: NeedInput не должен ждать парного HaveOutput.
  Выход обслуживается первым, входные кредиты не теряются на прежнем cap2.
  При новом запросе входа worker освобождает свой старый raw slot (MFT уже
  владеет копией) и берёт свежий, без новой очереди. Выход получает собственный
  sample PTS, проверяемый в принятом диапазоне и в порядке без B-кадров;
  его больше нельзя приписывать последнему поданному raw.
  Force-IDR property не повторяется для каждого input до первого output;
  periodic запрос использует существующее coalescing. Реальные HRESULT ошибки
  не замалчиваются. Временный busy bitrate сохраняет pending request для retry
  и не затирает более свежую seq.
-18 целевых случаев (13 live +5 MFT) прошли x64/x86; capture x86/x64/default
  собраны. Secret scan13 изменённых native файлов:0 находок.
  Подготовленный новый capture SHA256:
  `880D9500C22CF632E338DDE84454FF3A010864B79EE15E39AAFFD91AAB68A506`.
  Он пока **не установлен**; установленным остаётся44D3146... из паузы.
  Манифест обновлён с previous44D3146; через async вопрос запрошен повторный
  запуск root `capture-latency-control.ps1` в admin PowerShell, ожидается READY.
- Аппаратный узкий reproducer в native `.scratch/capture-pacing/repro-mft-input.c`:
  простой шаблон125→175→250:120 кадров на baseline и candidate проходят.
  При125 и переходе к текстовому шаблону baseline зависает на кадре20;
  новый event flow получает от драйвера настоящий ProcessInput E_FAIL на21.
  Тот же текстовый шаблон при500 на baseline проходит120 кадров.
  Это свидетельствует о проблемном сочетании сложности картинки и бюджета,
  **не доказывает универсальный аппаратный минимум125/500**. Такой минимум не
  добавляется. Синтетический шум тоже приводит к E_FAIL; его не выдавать за
  acceptance. Private reproducer использует общий encoder mutex и не отправляет RTP.
- Дополнена ещё не развёрнутая frontend policy: half-raster раньше по размеру
  известного IDR, а при восстановлении сначала bitrate budget **следующего**
  более крупного растра (`ceiling / 4^(step-1)`), затем проба растра на том же
  bitrate. Это устраняет прежний1500×1000@125 recovery. Шаг bitrate10% исходного
  ceiling; новых произвольных аппаратных порогов нет.19 policy cases и build
  прошли. Браузером эта версия пока не проверена; планируется local dist через
  `LATENCY_LOCAL_DIST`, без немедленного server deploy.
- Источник async модели: [Microsoft Asynchronous MFTs](https://learn.microsoft.com/en-us/windows/win32/medfound/asynchronous-mfts).
  Аппаратный E_FAIL после нового input не считается исправленным одним event pump;
  следующий гейт — актуальный candidate + корректный bitrate-before-raster recovery
  в реальной трансляции, затем оба codecs/displays и10мин.

## Пауза по просьбе пользователя — 2026-10-27

**Последнее состояние; имеет приоритет над историческими записями ниже.**
Пользователь попросил остановиться после текущей проверки. Новые прогоны,
подпись, публикация native и новые деплои не запускать до продолжения задачи.

- app1 PR156 слит/развёрнут как `20b40c1d78ff528237141c3a86012d4873500c92`.
  MenuBuilder PR134 слит/развёрнут как `6a6a9832ac8055a9969ec31a653bdc775c3549de`:
  backend и frontend. Это явно разрешённые merge/адресные деплои.
- Native worktree: `D:\repo\platerra\Public\etranprocessing\.worktrees\l4capture-desktop-pacing`,
  ветка `perf/l4capture-desktop-pacing`, HEAD `c7b859b` +9 изменённых файлов.
  Новые исправления **не закоммичены, не подписаны, не опубликованы**.
- На773 оставлен установленный unsigned кандидат, без отката:
  l4capture SHA256 `44D3146EF396A32987ED3AA0527ADE70FDDEFC979DD6CF680749942EEA948F6C`;
  leo4proxy `DA00DBBB318DA611349E46FAC1E685A4F02EFC88106D5736A6711ED09E6C27AB`;
  l4desk `AA0B644AAD511FCC5E9BAF7FACDD81F44B6F7DDBA33AE9D8B3ABFF9EB3DC43DC`.
  Манифест: native `.scratch/capture-pacing/runtime-candidate.json`.
  Оригинальные резервные копии сохранены. Контроллер получил штатный `exit`;
  для следующей установки снова запустить корневой `capture-latency-control.ps1`.
- Native добавляет optional loopback send-clock RTP extension L4QT, который proxy
  удаляет перед WAN. Это отделяет очередь localhost/TLS от времени encode.
  Старый producer остаётся на прежнем пути. Исправлено распознавание H264 STAP-A
  при восстановлении зависимостей; такой формат уже выпускает packetizer.
- Выявлено **не устранённое зависание MFT после возврата разрешения/bitrate**.
  Старый helper завершался на `output_wait age_ms=546` как `device_lost`.
  В текущем кандидате HEVC использует внешний существующий watchdog вместо
  внутреннего500мс; отложены force-keyframe writes до следующего ProcessInput.
  Повтор `raster8-mf-fixed` показал `MF_RUNTIME_WAIT age_ms=547`, затем через3с
  `HEVC_WATCHDOG ... sent=2133 encoded=2133` и STOP13. Значит,500мс были
  преждевременной классификацией, **но не корневой причиной отсутствия output**.
  Нельзя считать исправленным и нельзя просто увеличивать watchdog дальше.
  Последняя рабочая конфигурация перед зависанием: DISPLAY1,1500×1000,175кбит/с;
  перед этим восстановление750×500@125 →1500×1000@125 →175.
  Следующий раз исследовать MFT input/output/event state, а также режим rate
  control и принятие новых samples при низком bitrate; не вводить vendor-minimum
  из предположения и не ослаблять реальные ошибки устройства.
- 12 focused live cases прошли x86/x64 после последнего native изменения;
  UDP feedback isolation/strip — x86/x64; x86/x64/default capture и proxy собраны.
  Последний secret scan9 изменённых native файлов:0 находок, diff-check чист.
  `leo4proxy build.cmd all` скомпилировал обе архитектуры, но общий пост-тест
  остановился на неуказанном `L4TOOLS_TEST_CERT_THUMBPRINT`. Сертификатные тесты
  не менялись и не перенастраивались. После guard изменения выполнены штатные
  `build.cmd x86`/`x64` успешно; целевой feedback тест отдельно обе архитектуры.
- Frontend рабочая ветка теперь `fix/desktop-receiver-queue` от origin/main6a6a983.
  **Изменены только policy и её тесты, Janus lifecycle не менялся**:
  измеренный большой `last_idr_bytes` позволяет half-raster уже при первой
  pressure отсечке; используется прежний бюджет200мс. При отложенной sharper
  пробе clear recovery больше не уменьшает bitrate до tier ceiling.
  19 policy cases и локальная production сборка прошли до завершения паузы.
  Это ещё не протестированный браузером/не развёрнутый код.

### Реальные прогоны и ограничения evidence

- `raster5-diag`, DISPLAY2 H265, A/B/A90с: все фазы без RTP-потерь/обрыва.
  A1:10,01 decodedFPS/1,665Mbps; B:9,66FPS/0,389Mbps, half-raster960×540;
  A2:9,79FPS, возврат1920×1080 и2000кбит/с примерно к72,5с.
  Дополнительный лаг по неизменной RTP-шкале: B медиана409мс/p951601/пик4440;
  A2 медиана127мс/p95561. Это ещё версия до последнего MFT изменения.
- `raster8-mf-fixed`, DISPLAY1: A1 без потерь и смены качества,3000×2000@2000.
  B снизился до1500×1000, затем750×500. **A2 оборвался описанным MFT hang**;
  полный A/B/A этой версии не пройден. H264 browser и10мин текущей версии
  ещё не выполнены. Ранний raster4 тоже оборвался в A2.
- Receiver-only reset проверен **только частным экспериментом**: stop/start
  текущего Janus playback client из собственного React ref, исходный издатель
  остался тем же; signaling0,87с. Это не production реализация и не доказанный
  выигрыш. Новый SSRC меняет RTP origin, поэтому нельзя сравнивать старый
  now−RTP offset после reset: он скрывает задержку.
- `raster6-reset`/`raster7-gdi-reset` — диагностические, не acceptance:
  чтение video через canvas занимало140–245мс, GDI окна с video73–118мс.
  Они сами создавали нагрузку; их FPS/lag не выдавать за улучшение продукта.
  Частный visual-clock helper сохранён, source clock теперь opt-in100мс,
  GDI samples не чаще10с. Для основных прогонов всё это отключать.
- Chrome155.0.8059.85, CDP9335, авторизованное окно сохранено открытым.
  Сетевое ограничение явно снято,773 штатно остановлен, тестовые оверлеи,
  rVFC polling и source animation выключены; host sampler остановлен marker-файлом.
  При продолжении удалить только свой `host-raster.stop` перед запуском sampler.
- Частные harness/results: worktree `desktop-video-observability`,
  `MenuBuilder/frontend/.pytest_tmp/desktop-performance-native`.
  Native helper/scripts/logs: `.scratch/capture-pacing` в native worktree.
  `summarize-raster.py` выводит отдельные RTP/decode/video-drop/freeze/lag;
  его общий RTP lag применим только без receiver reset.

### Следующие действия после продолжения

1. Диагностировать настоящий MFT output stall; не объявлять увеличение ожидания
   исправлением. Проверить, не ожидает ли transform дополнительные inputs/events.
2. Проверить local dist раннего raster policy на реальном сервере/агенте через
   `LATENCY_LOCAL_DIST`; сначала исправить hang. Не мерджить текущую гипотезу
   до browser evidence, потом оформить отдельный follow-up PR/frontend deploy.
3. H264 software/H265, оба дисплея, STOP во время transition; A/B/A90с и10мин.
   Отдельные native traces — вне performance прогонов, чтобы не вносить нагрузку.
4. Финальный review/тесты только изменённого; подпись/публикация native по-прежнему
   отложены. Не удалять сохраняемые compose candidate JSON: они нужны текущему
   production deploy; временный host deploy wrapper убрать при завершении задачи.

## Актуализация 2026-10-27: развёрнуто, приёмка не завершена

Эта запись заменяет прежние отметки ниже «ещё не развёрнуто».
После явного разрешения пользователя слиты app1 PR156 (`20b40c1`) и MenuBuilder
PR134 (`6a6a983`), адресно развёрнуты app1, backend и frontend MenuBuilder.
Соседние контейнеры и текущие настройки сохранены. Native `c7b859b` установлен
на773 без подписи; публикация native-релиза остаётся отложенной.

Два DISPLAY1 H265 A/B/A90с (второй без tracing) выявили нежелательные снижения
качества уже на обычном канале. При этом по фактическим synchronization sources
не обнаружен соответствующий рост браузерной RTP-очереди. В отчётах транспорта
появлялись pressure/age0,4–0,8с. Диагностический замер native показал:
первый IDR после reinit encode484мс/input-age584мс, следующий16мс/61мс.
Прежнее сравнение capture PTS с временем чтения proxy смешивает эту задержку
кодера с очередью транспорта. Поэтому результат адаптации пока не принят;
длительный прогон и второй дисплей этой версии не являются пройденными гейтами.

В работе локальное исправление: optional loopback RTP send-clock, удаляемый
proxy перед WAN, отделяет фактическую локальную очередь от холодного encode.
Старый producer сохраняет прежний путь. Порог200мс не увеличен, новых writers нет.
Дополнительно выявлен и покрыт тестом H264 STAP-A keyframe repair: packetizer
уже выпускает STAP-A, а прежний детектор proxy не распознавал его как repair.
Целевые12 native cases x64 и UDP feedback x86/x64 прошли; финальные сборки,
повторный сквозной прогон и обновление evidence ещё выполняются.

## Контекст задачи

- Владелец требования: пользователь. Приоритет — минимальное накопление задержки;
  временное снижение разрешения явно разрешено 2026-10-27.
- Шаг должен существенно улучшать сжатие. Последовательные шаги — примерно 2:1
  по каждой стороне, то есть вчетверо меньше пикселей при неизменном FPS.
- Единственный управляющий обратный канал — первичный инициатор сессии.
  Наблюдатели и транспорт поставляют измерения, но не команды качества.
- Поддержать актуальный агент с HEVC/MF и программным H264/OpenH264.
  Старым агентам достаточно прежнего H264: новые возможности для них необязательны.
- Работа в отдельных worktree; native подпись и публикация релиза отложены.
  Проверки браузером разрешены. Этот документ — схема для реализации/ревью,
  а не утверждение о готовности смены растра.

## Выполнено и наблюдаемое evidence

- Реализованы перекрытие захвата/кодирования HEVC, заменяемый последний сырой кадр,
  runtime bitrate и проверяемый сервером оригинальный writer key.
- app1 PR154/e446f6b и PR155/c86cd55 развёрнуты; backend MenuBuilder — PR132/6ef662a.
  Frontend PR133 слит как1b6288c, но не развёрнут: сборка/тесты прошли, push
  остановился на DNS registry. Backend пересобирать без изменения кода не требуется.
- На773, Chrome155.0.8059.85, при1Мбит/с старый контроллер накопил32с отставания
  без RTP-потерь. Поэтому одних packetsLost/framesDropped недостаточно.
- Последний DISPLAY1/3000×2000 A/B/A: ограниченная фаза около9FPS, новых RTP-потерь0;
  дополнительная задержка относительно начального RTP-времени: медиана0,488с,
  p954,47с, пик5,45с. После снятия ограничения bitrate восстановился до2000кбит/с.
- Максимальный IDR около470КБ: передача такого кадра по1Мбит/с занимает примерно3,8с.
  Уменьшение будущего bitrate не удаляет уже отправленный большой кадр из очереди.
- Нативный commit34182bb установлен unsigned. Последующий незакоммиченный код
  добавляет I420 worker/OpenH264 SetOption, переинициализацию вне control thread,
  восстановление прежней конфигурации при отказе, area/box scale и optional raster IPC.
 13 live cases прошли x86/x64;10 scale cases — x64; adapter v1/v2/старый helper — обе архитектуры.
- В текущем коде добавлены реальный GDI/OpenH264 probe и современный H264 START,
  включая привязку фактического profile_level_id. Offline capability fixture без
  аппаратного fingerprint прошла x86/x64. Реальный browser H264 ещё не проверен.
- app1: optional raster command/report, исходный START неизменяемый, software H264
  discovery без HEVC chain flag;190 затронутых тестов прошли. BFF:4 теста прошли.
  Frontend:17 policy cases прошли, включая assembly до первого decode.
  Изменения этого расширения ещё не развёрнуты и не установлены на773.

## Предлагаемый контракт

| Контракт | Producer → consumer | Совместимость |
|---|---|---|
| runtime quality v2 | инициатор → BFF → app1 → l4desk → l4capture | Опциональное расширение существующего канала: bitrate и ступень растра одной операцией |
| application report v2 | первый подтверждённый новый IDR → l4desk → app1 → UI | generation, фактический coded raster, bitrate, статус, размер IDR; accepted не равен applied |
| live geometry snapshot | app1/quality API → UI/viewers | Отдельно от неизменяемого исходного START descriptor; расширение должно быть явно согласовано всеми новыми участниками |
| native feature discovery | новый helper → новый агент | Отдельный opt-in; старый probe/START не получают обязательных полей |

Текущая реализация сохраняет MQTT envelope `v:1`: optional `raster_step` в прежней
команде, `quality_version:2` и полный набор coded geometry в отчёте. Старому helper
новые IPC-команды не отправляются: отдельный `--probe-raster-quality` включает
CMD0x0007/0x0008 и EVENT0x0106 (48байт); прежний отчёт28байт сохранён.

Начальный START v3 нельзя молча переопределить. Его source, capture geometry,
codec/profile, lease и stream epoch остаются исходными. Семантику разрешённого
runtime coded raster нужно явно оформить отдельным расширением и проверить
на producer/consumer, включая media/Janus. Старым агентам команды расширения не
посылаются; неподдерживаемая адаптация не должна мешать обычному START.

## Математика ступеней и масштабирование

| Исходный coded raster | Ступень1 | Ступень2 | Относительное число пикселей |
|---|---|---|---|
|3000×2000|1500×1000|750×500|1 →1/4 →1/16|
|1920×1080|960×540|480×270|1 →1/4 →1/16|

- Ступени считаются от исходного coded raster; capture rect экрана не обрезается.
- Для точного2:1 использовать area/box2×2: низкочастотная фильтрация перед
  уменьшением убирает алиасинг и лишнюю высокочастотную информацию для кодека.
- Для размеров, не делящихся удобно, округлять до допустимой чётности4:2:0;
  фильтровать весь исходный прямоугольник, проверять края и погрешность пропорций.
- Кратность16/32/64 не вводить универсальным ограничителем: учитывать реальные
  требования энкодера. Дополнительное padding/cropping требует явного описания.
- При том же FPS четверть пикселей даёт ориентир четверти бюджета bitrate для
  сохранения сопоставимых bits/pixel. Это гипотеза для измерения, а не обещание
  четырёхкратного выигрыша канала. Проверять фактические IDR/AU, текст и CPU.

## Предлагаемая транзакция

1. Инициатор видит рост очереди и выбирает целевую ступень/bitrate. RTP-потери,
   декодирование и показ измеряются отдельно; потеря сама не доказывает перегрузку.
2. app1 проверяет исходный writer key, owner/org/session, действующие lease/stream,
   generation и объявленную поддержку. Фиксирует pending, не applied.
3. Агент проверяет ту же эпоху и срок команды. Capture/input geometry неизменны.
   Переконфигурация не блокирует обработку STOP/renew; сырой backlog не растёт.
4. Владелец энкодера завершает/освобождает старый AU, отбрасывает старый pending,
   меняет формат и использует свежие пиксели с новыми stride/plane sizes.
   Перенос старого NV12/I420 буфера в новый формат недопустим.
5. После фактического нового SPS/PPS(+VPS)/IDR проверяются raster/codec/цвет и
   целостная отправка. Только затем публикуется подтверждение новой геометрии.
6. UI обновляет live coded geometry. Ввод продолжает отображаться в координаты
   исходного физического дисплея. Удержанные клавиши/кнопки безопасно сбрасываются
   при паузе; неверный размер видео не должен давать неверный remote click.

### Что ещё нужно решить/подтвердить кодом

- Сохранение одной сессии с новым SPS предпочтительнее STOP/START аренды:
  нельзя потерять original-writer binding, route или состояние других viewers.
- Старый приёмный backlog сам по себе не исчезает после изменения encoder.
  Проверить локальное пересоздание WebRTC-приёмника инициатора после подтверждённой
  смены растра: без остановки издателя и без нового канала управления качеством.
  Это предложенная мера, ещё не доказанное решение для текущего Janus.
- Для реакции до завершения большого IDR проверить сигнал незавершённой сборки:
  пакеты приходят, но следующий полный кадр не появляется. Не путать его с
  decoder/compositor stall и отсутствием изменений источника.
- Возвращать по одной ступени через контролируемую пробу; низкий трафик статичного
  desktop не доказывает запас канала. Порядок возврата bitrate/raster и паузу после
  неудачной пробы определить по A/B/A, не объявлять непроверенным preset.
- Кандидат policy: half-size при достижении четверти исходного bitrate на ступень;
  нижний предел прежнего bitrate100кбит/с ограничивает число автоматических ступеней.
  Проба более чёткого растра сохраняет bitrate, после неудачи интервал удваивается.
  Это код для проверки, а не принятый результат измерения.
- `framesReceived` отделяет сборку целого кадра от decode. При продолжающемся RTP
  без новых полных кадров и без новых потерь кандидат измеряет затянувшуюся сборку;
  это не вывод из `framesDropped`. Основание: [W3C stats](https://www.w3.org/TR/webrtc-stats/#dom-rtcinboundrtpstreamstats-framesreceived).
- Ошибка драйвера/выделения памяти: откат к последней рабочей конфигурации,
  явный отказ изменения качества, сохранение обычного видео где восстановление возможно.

## Инварианты безопасности

- Никаких новых quality writers, SU takeover, управления от proxy/наблюдателей.
- Ни смены source/display, ни расширения capture rect, ни продления lease командой качества.
- STOP/expiry и конечный gate перед RTP сохраняют приоритет над результатом reinit.
- Codec,8bit4:2:0, отсутствие B-кадров и допустимые профиль/уровень сохраняются.
- Секреты, writer key, PIN и JWT не попадают в отчёты/логи/этот handoff.

## Проверено / оставшиеся гейты

- [x] Битрейтный канал: wrong-writer403, generations/lease/epoch, старый helper.
- [x] Queue-only/floor policy:13 frontend cases +2 lifecycle, production build.
- [x] Software H264 worker/rate и reinit/rollback:13 native focused cases x86/x64.
- [x] Математика/low-pass:10 scale cases x86/x64; NV12/I420 ownership и overflow bounds.
- [x] Capability без GPU fingerprint, точный H264 START и frozen schema ACK: offline x86/x64.
- [x]17 frontend policy cases;190 app1 video/quality,20 quality повторно после расширения START gate.
- [ ] Реальные MF reinit, STOP/renew/expiry во время смены и поздние результаты.
- [ ] Media/Janus и viewers; remote input: центр/углы, DPI1/2, drag/held keys.
- [ ] Реальные H264 software/H265, оба дисплея, плохой канал ещё до первого кадра.
- [ ] A/B/A90с и10мин комбинированной версии; RTP/decode/show/CPU/GPU раздельно.
- [ ] Финальный source review, scoped checks, адресный деплой и cleanup.

## Рабочие указатели

- [Активная задача](2026-10-26-desktop-runtime-quality.md).
- [Текущий битрейтный контракт](../../../docs/etran_arch-desktop-runtime-quality.md).
- Native worktree: `../../../../l4capture-desktop-pacing`; agent/native files:
  `tools/l4capture/src/{main.c,pipeline/live_encoder.c,encoder/*}`,
  `tools/l4desk/src/{video_capability.c,l4capture_adapter.c,ctl_protocol.c}`.
- Владельцы: app1 — авторизация/pending/report; MenuBuilder — BFF/инициатор/viewer;
  l4desk/l4capture — применение; media/Janus — доставка и decoder transition.
