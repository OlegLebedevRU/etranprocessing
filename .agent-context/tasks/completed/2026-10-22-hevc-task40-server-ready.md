# TASK-40 — G-SERVER-READY=PASS

Дата проверки: 2026-10-22, UTC. Владелец: серверная интеграция
IoT → BFF → l4media/Janus → MenuBuilder. Native TASK-50/51 не выполнялись.

## Целевой контур и исходники

UI: <https://dev.leo4.ru:3000/video?device_id=773>. Проверен установленный
Chrome155 с WebRTC H.265. Авторизация — обычный вход владельца, tenant1.
По прямому разрешению пользователя использован существующий терминал773:
certificate SN `a4b0000773c82116d210826`, terminal_id1, IoT device row178.
`773` — device_id, не certificate SN. Сертификат и локальные proxy/broker сохранены.

TASK-00 `3e236dc5b3b92afc83d49b022b38d07bccfa82c4`, TASK-20
`bbc8f0d8a99a0b6a314a3ecf0d1f39add650b404` и TASK-30
`d2cf9f7e75b799306a5722688da4dff03a0a468a` являются предками серверной
ревизии etran ниже. IoT содержит TASK-10
`166e2ccc149d79d3b718ab6d0fe515ad03e4c904` и его G0 cherry-pick
`7b5f1224666362ecb71f390057ed3ba863cf1ff6` исходного
`e1cc22d6b5fc49bd3558402f9a8ca12247e6a97e`.

Frozen contract: `hevc-contract/2.0.0`, SHA-256
`f065b967cd10c2c709fb394ef9e6c2ecf74f77de060d88c9a821cc9069766ac4`.
Повторная проверка двух пакетов дала полное совпадение. G0 не изменён.

| Компонент | Полная source revision | Immutable registry digest |
|---|---|---|
| app1 | `de8850eec9163642a6eed9be8dae57244e1e50c9` | `sha256:7d62516e5ad5a93dda2c52a745cae64b9abe46aeb4aefcf4b39a343c7d8bceb7` |
| l4media-ingress | `dcd3ea1c04a6be1e3fe3047f3d5a3bec8747081d` | `sha256:210420e41d6870b45f4558e1441d51dc2e1cbc8b06402303434cd6f28f995ed2` |
| BFF + JWT signer | `4fea1612a911d4b88a3e2cf2c61bf58e2c1a8316` | `sha256:35245528fbf7eb8f4d0a9aa5d3ca2b4561b0730b2155723b39ef12581d3e8627` |
| frontend | `4fea1612a911d4b88a3e2cf2c61bf58e2c1a8316` | `sha256:f5934ab38a220ef78de7b92b09acbc43cb8960402ebacc56eb5d7d85a910ca0b` |
| Janus1.1.4, сохранён | `e6e681dcf74fb0e81da5cf1f7f0fc2d39dca7f33` | `sha256:93265665a92482ec1de2c9571a08d27c87b1dee06efc000f717ed42dfe2438ae` |

Образы etran находятся в registry namespace `etran`; Janus — в исходном
namespace. Это OCI digests; config digest и локальный image ID не подменяют их.
Фактические assets: `index-Dv9DQupg.js`, `video-surveillance-Cua-6g09.js`.
SHA-256 index.html:
`d4c9acc5d864650ae81aafaf6349f537ce7963d812ca4a65415f94427382e0c3`;
manifest:
`0d5020498227314a83f8a42e8fe5bf2e30b526b5ce056abfaf0ad887b5a9e6ce`.

Сборка и публикация выполнены на разрешённом builder176 штатными launcher и
`deploy/registry_app1.py`, production использовал pull по digest. Readers
развёрнуты до включения v2 writer. Никаких SQL/Alembic изменений не требовалось.
Janus, Nginx, proxy, broker, DB и соседние приложения не пересоздавались.

## Фактическая приёмка

- [x] Настоящая RTSP камера: HEVC Main8bit420, 1920×1080@20, без B-кадров,
  повторяемые VPS/SPS/PPS, IDR каждую секунду. `backend=camera`, `hardware=false`.
  Явно тестовый source `test-rtsp-camera`, без desktop/input capability.
- [x] FFmpeg использует `-c:v copy`; программного HEVC encoder нет.
  `hevc_metadata=level=4` нормализует объявленный Level4.1 до контрактного4.0.
  VCL до/после побайтово одинаков: SHA-256
  `bdc0c6213e02cff0312f709f49b79e49efa48a7f6814dc6858c609870d1ad5f9`.
- [x] Реальный путь: камера → локальный RTP → существующий leo4proxy → TLS/
  L4RTP/1 → ingress → Janus → целевой UI. Живые timestamps/sequence/SSRC,
  новый producer при ручном повторе; прямого camera→browser обхода нет.
- [x] В Chrome negotiated `video/H265`, Main/profile1, level120. Первый поток
  показал129 кадров, затем1828; повторная проверка финального UI дала три
  подписки с942/207/188 decoded frames при1920×1080. READY отдельно проверялся
  по SPS/IDR, но доказательством decode считаются браузерные счётчики.
- [x] После доставки физической камерой читаются контрольные `T40`, `A17`, `B42`.
  Кадр снят из декодированного video элемента1:1. В этом потоке счётчик вырос
  с10 до3863 кадров. Таблица была на физическом мониторе, не наложена на decoder.
- [x] Три подписки одной разрешённой owner session, позднее подключение,
  уход одной и повторное позднее подключение. Producer не перезапускался;
  индивидуальные Janus SSRC оставшихся подписок сохранялись, кадры росли.
  Допуск независимых пользователей этим тестом не доказан и не расширен.
- [x] Контролируемый producer failure остановил все три подписки. Каждая
  сохранила `test_camera_source_failed` и закрыла PeerConnection. За74.7с
  нового producer/автоматического fallback не было. Ручной повтор дал новый
  stream UUID/SSRC; повторная отправка ошибки старого UUID не закрыла его:
  decoded frames164→713, SSRC2158730916 неизменен.
- [x] Неверный source и неподдерживаемый low profile: HTTP422
  `source_mismatch` / `profile_unsupported` до START. Старый настоящий l4desk
  отклонил explicit HEVC с422 `capability_mismatch` до START.
- [x] Старый l4desk1.13.43 и прежний frontend: запрос содержит только
  mode/source_id/profile/lease_id, без codec_policy. H.264 decoded frames3→54,
  прежний клиент освободил аренду; запрос без cookie получил401.
  Для загрузки прежнего клиента браузеру передан сохранённый исходный index
  с проверенным SHA; JS/assets, API, авторизация и media — реальные production.
  Ответы API и медиапоток не подменялись.
- [x] Финальный новый UI со старым агентом: Auto до START разрешился в H.264,
  decoded frames3→49; типизированная v2 capability старому агенту не приписана.
- [x] Изолированный smoke точного опубликованного ingress digest с pinned Janus:
  Redis crash/reload, три HEVC SDP offers, L4RTP forwarding, stale stop,
  Janus failure/restart и отсутствие resurrection после stopped reload.
  Это recovery integration evidence, не production outage drill и не decode.
- [x] Deploy gates: финальный BFF826 passed/22 skipped, Ruff/format/Pyright;
  frontend102 tests и TypeScript/Vite build; app1 Linux714 passed/33 skipped,
  scoped Ruff/Black и runtime OpenAPI. G0:30 schema,6 legacy,24 PREPARE,
  9 lifecycle,61 unknown-property mutations, duplicate-key refusal, peer match.
- [x] app1 OCI-aware read-only scan: все13 слоёв, private-key/.env findings0,
  build proxy не остался в runtime ENV. Secrets в изменениях не зафиксированы.

## Устранённые интеграционные расхождения

1. BFF использует реальные negotiated методы IoT `/internal/v1/...`, без
   выдуманного `/internal/v2` и без fallback на legacy после403.
2. HTTP `VideoStateView` отделён от MQTT READY: переносит identity исходного
   START, полный descriptor и confirmation только после реального READY.
3. Старый общий status не содержит owner_session_id. Добавлен read-only
   `GET /lease/{id}/video-lease`: exact tenant/user/session, включая superuser.
   Это HTTP projection, не изменение G0; старый status не расширен.
4. Query `contract_version=2` разбирается как ограниченный int, а не Literal,
   отвергавший строковое значение. Оригинальные HTTP варианты проверены ASGI.
5. View lease не обращается к publisher-only reader; retained failed v2 record
   не замещает нового H.264 publisher на той же аренде.
6. BFF watch передаёт только terminal state/reason/валидный stream UUID из
   существующего IoT события. UI закрывает совпадающий epoch и сохраняет причину
   после release общей аренды. Live state и новые START остаются REST-authoritative.
7. G0 не содержит нормального STOP ACK/stopped envelope. Тестовый источник
   фиксирует физический выход локально, не придумывая новые wire события.

## Работа следующего исполнителя

Открыть указанный UI в Chrome, terminal773, основной экран. Auto/H.264 доступны
со старым агентом. H.265 для него недоступен до честного TASK-50 hardware probe.
Для native HEVC нужны Media Foundation hardware capability и encoded sequence;
доступность камеры не доказывает пригодность GPU/MFT. Native исходники менять
в отдельном l4tools репозитории по TASK-50/51, не в копии tools данного проекта.

Итоговые флаги: BFF v2=true, HEVC allowlist только согласованный certificate SN,
proxy RTP/RTCP5004/5005; app1 v2=true, HEVC_CHAIN_READY=true. Это утверждение
о проверенном серверном тракте. Оба test-camera allowlist очищены до `[]`.
Ingress HEVC_ENABLED=1. Старый агент не объявляет v2 и остаётся на H.264.

Runtime overrides: `.etran-ci/hevc-task40-main-flags.json` и
`.etran-ci/hevc-task40-media-flags.json` в штатном production state каталоге.
Флаги не содержат секретов. **Обычный beta deploy не подключает эти дополнительные
override автоматически**: после будущего релиза соответствующего контейнера
нужно применить тот же reviewed override через compose с `--no-deps --no-build`
и повторно проверить фактический ENV. Приватные `.env` и base compose не менять.

Повтор камеры: [адаптер и ограничения](../../../l4media/testing/README.md).
Получить обычный owner login, убедиться в отсутствии активной аренды; штатным
RPC7001 временно остановить только L4Superv/l4desk. Не менять proxy/broker/cert.
Временно вернуть этот SN в оба test-camera allowlist, применить reviewed
overrides. Приватный camera JSON передать через `--camera-config`, не через CLI
URL. Запустить `uv run --script l4media/testing/rtsp_test_source.py` с путями/SN
из окружения; в UI выбрать TEST camera, HD, H.265, Запустить. После STOP закрыть
адаптер маркером STOP, восстановить L4Superv и очистить camera allowlists.

## Rollback и очистка

Исходные app1/BFF/ingress digests и frontend tar сохранены до rollout в
`.etran-ci/hevc-task40-rollback/`. app1 rollback действительно выполнен:
`8cd723367c09f5aa45b71c98dc4d3a89ac69612d` /
`sha256:3f92a4f1df06f0231caf74e4f8a4c0be5085d5700aeb26b59273f06034255eef`,
health подтверждён, затем восстановлен финальный digest из таблицы и flags.
Полный откат всех readers одновременно не выполнялся.

При rollback сначала выключить новые v2 START, штатно остановить собственные
потоки, дождаться отсутствия leases/routes/mountpoints; затем откатывать writers
и совместимые readers штатным digest deployer. Не удалять чужие Redis keys,
не откатывать DB schema, не пересоздавать Nginx/broker. Для старого BFF вернуть
совместимый frontend из сохранённого tar; assets публиковать до index.html.
После отката сверить revision/digest, health, lease и реальные H.264 frames.

Финальная очистка: собственные leases и media Redis owner/route keys отсутствуют,
route_exists=false, fresh_rtp=false; в Janus остался только mount1,
не относящийся к тестовым динамическим mountpoints и не удалявшийся задачей.
Diagnostic stopped tombstone оставлен штатному TTL. Адаптер и его FFmpeg закрыты,
L4Superv/l4desk восстановлены; PID proxy194860 и Mosquitto222220 не изменены.
Тестовое окно закрыто, браузер оставлен на обычном idle UI773. Изолированный
test-backend и его override удалены, экспорт login state удалён. Временные
CONNECT proxy, SSH reverse tunnel и BuildKit network relay остановлены.
Сборочные caches/immutable artifacts и приватный evidence сохранены для аудита.

Автоматическая проверка отклонила удаление собственного `.pytest_tmp` с причиной
`blocked by policy`; каталог остался untracked в TASK-40 worktree.

## Evidence и ограничения

Очищенные числовые evidence/receipts находятся в локальном
`D:/.codex/hevc-plan/evidence/task40`: acceptance-summary.json,
hevc-final-three-watchers.txt, hevc-final-failure-watchers.txt,
hevc-final-no-autorestart.txt, hevc-final-retry-before-stale.txt,
hevc-final-retry-after-stale.txt, hevc-optical-stats.txt,
legacy-h264-after-rollout.txt, legacy-release-rights.txt,
auto-h264-regression.txt, hevc-negative-negotiation.txt,
legacy-hevc-unsupported.txt, inventory-before.json/inventory-final.json,
final-receipt.txt и cleanup-server.txt. Camera/auth values отсутствуют в отчётах.
Кадр optical proof содержит приватный интерьер/экраны: хранится отдельно с ACL,
не публикуется в Git. Сохранённый camera JSON тоже приватный.

Ограничения: это серверный gate с camera passthrough, не приёмка hardware desktop,
не независимые viewer leases и не длительный нагрузочный тест. Edge155 в данной
установке не объявляет H.265 WebRTC; подтверждён Chrome155. После frontend deploy
нужен hard refresh: старый index мог оставаться в browser cache. Nginx не менялся.
В npm lock остаются7 audit findings (1 moderate,4 high,2 critical); зависимости
в этой задаче не обновлялись. Старый image scanner не распознаёт OCI layer layout;
для app1 выполнен эквивалентный scan всех реальных слоёв, а отказ старого scanner
не засчитан как PASS. Остальной исторический IoT lint debt не исправлялся массово.
