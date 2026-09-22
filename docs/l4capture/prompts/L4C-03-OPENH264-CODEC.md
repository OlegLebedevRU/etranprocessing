# L4C-03-OPENH264-CODEC — Статический C API OpenH264, сжатие I420 в H.264 Constrained Baseline 3.1, нормализация NAL/AU и IDR-каденция

```yaml
prompt_id: L4C-03-OPENH264-CODEC
scope_project: tools/l4capture
scope_root: D:\repo\platerra\Public\etranprocessing\tools\l4capture
prompt_type: implementation-step
required_handoff_ids:
  - H-L4C-02-v1
sequence_gate_status: READY_FOR_L4C_03
output_handoff_id: H-L4C-03-v1
next_prompt_id: L4C-04-RTP-SENDER
branch: l4capture/l4c-03-openh264-codec
report_path: docs/l4capture/handoffs/L4C-03-OPENH264-CODEC-report.md
candidate_format: DETACHED_V1
candidate_path: docs/l4capture/handoffs/L4C-03-OPENH264-CODEC-candidate.md
architecture_sections: [1, 2, 3, 5, 6, 7, 8, 10, 12, 13, 14]
consumers:
  - L4C-04-RTP-SENDER
  - L4C-05-AGENT-ADAPTER
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
```

---

## 1. Цель и архитектурная миссия

Ты выступаешь в роли **Ведущего инженера видеокодеков и сжатия медиа (L4Capture-OpenH264-Agent)** в рамках комплекса `tools suite`.

Твоя задача — реализовать native-модуль программного сжатия видео H.264 на базе библиотеки **OpenH264 v2.6.0** через чистый C API wrapper в целевой директории `tools\l4capture\`. Программный энкодер OpenH264 является универсальным гарантированным бэкендом кодирования (baseline fallback), обеспечивающим совместимость со всеми версиями Windows (начиная с Windows 7 SP1 x86/x64) независимо от наличия аппаратных энкодеров GPU (Intel QuickSync, NVIDIA NVENC, AMD AMF).

На шаге **L4C-03-OPENH264-CODEC** создаются и интегрируются ключевые компоненты компрессии видеопотока согласно архитектурным требованиям `l4capture_arch_final.md`:
1. **OpenH264 Encoder Backend (`openh264_encoder.c`):** Реализация интерфейса `IEncoderBackend` (`include/l4capture/encoder_backend.h`) через C API обёртку над OpenH264 v2.6.0 (`codec/api/wels/codec_api.h`).
2. **Профиль и параметры H.264:** Настройка энкодера под профиль `base_480p` (`854x480` при 10 FPS, целевой битрейт 500 кбит/с, максимум 700 кбит/с) в строгом соответствии с профилем **Constrained Baseline Profile** (`PRO_BASELINE`, `profile_idc = 66`), Level 3.1 (`LEVEL_3_1`, `level_idc = 31`), совместимым с SDP `42e01f`. Один пространственный слой (`iSpatialLayerNum = 1`), запрет SVC, запрет B-кадров, запрет reorder/lookahead, энтропийное кодирование CAVLC (`iEntropyCodingModeFlag = 0`).
3. **Нормализация NAL и упаковка в Access Unit:** Разбор выходного битового потока OpenH264, **удаление (stripping) префиксов стартовых кодов Annex B (`00 00 00 01` и `00 00 01`)**, формирование массива дескрипторов `l4c_nal_desc_t` (указатели `data` указывают непосредственно на заголовочный байт NAL, `length` содержит чистую длину полезной нагрузки NAL для RFC 6184 RTP-пакетизатора шага L4C-04), извлечение `nal_type` (SPS=7, PPS=8, IDR=5, non-IDR=1) и установка флага `is_idr`.
4. **Управление ключевыми кадрами и каденция IDR:**
   - Гарантия наличия SPS + PPS + IDR в самом первом закодированном Access Unit.
   - Периодическая генерация IDR не реже одного раза в 2.0 секунды (2000 мс) монотонного времени, даже при пропуске кадров (frame skip) или снижении FPS.
   - Повторение актуальных SPS/PPS перед каждым IDR-кадром.
   - Коалесцирование запросов внепланового IDR (`force_idr()` / `CMD_FORCE_IDR`): объединение запросов с лимитом не чаще 1 раза в 500 мс (`L4C_FORCE_IDR_MIN_INTERVAL_MS = 500`).
5. **Обработка пропуска кадров (Frame Skip):** Корректная обработка решения rate controller OpenH264 о пропуске кадра (`videoFrameTypeSkip` или 0 байт на выходе): frame skip не считается ошибкой, транслируется в `L4C_ERR_NO_FRAME` (или AU с `nal_count = 0`) и учитывается в метриках как `encoder_drops`.
6. **Ресурсная дисциплина и Zero-Allocation:** Однократное выделение контекста энкодера и буферов AU (до 2 МиБ `L4C_MAX_AU_SIZE`) при инициализации; полный запрет `malloc`/`free` во время вызова `encode()`.
7. **Supply Chain, SBOM и патентный допуск:** Использование OpenH264 v2.6.0 (исправление CVE-2025-27091), статическая компоновка `/MT`, **компоновка в релизный бинарник `l4capture.exe` строго без декодера (encoder-only)** для снижения поверхности атаки. Документирование лицензии BSD-2-Clause (`OPENH264_LICENSE.txt`) и предупреждения об условиях патентного пула AVC/H.264.
8. **Автономный тестовый набор (`tests/`):** Полный комплект модульных тестов: корректность NAL-стриппинга, SPS/PPS валидация, каденция IDR, коалесцирование force-IDR, декодирование внешним эталонным декодером (`ISVCDecoder`) и 100-кадровый тест стабильности памяти.

---

## 2. Непереговорные рамочные принципы и изоляция

1. **Строгая изоляция директорий (Directory Boundary):**
   - **Код, заголовки, сборка и тесты:** размещаются строго в `tools\l4capture\`.
   - **Документация, промпты, журнал контрактов:** находятся в `docs\l4capture\`.
   - **Отчёты и candidate-файлы:** формируются строго в `docs\l4capture\handoffs\`.
   - **КАТЕГОРИЧЕСКИЙ ЗАПРЕТ `l4desk-service`:** Строжайше запрещено создавать, изменять или использовать файлы внутри каталога `l4desk-service`. Проект `l4capture` полностью изолирован.
   - **ЗАПРЕТ на модификацию других подсистем:** Запрещено изменять файлы в `BACK\`, `FRONT\`, `tools\l4desk`, `ProcessingBackend\`, `MenuBuilder\`, `shared\`, `sqlFileExample\`, `stored-procedures\`.
2. **Стандарты чистого C и компиляции:**
   - Код обёртки пишется строго на **C99/C11** (подмножество MSVC, компиляция через `cl.exe`, не C++).
   - Статическая компоновка Runtime: обязательный флаг `/MT` для Release-конфигурации (никаких динамических зависимостей `msvcrt*.dll`, `vcruntime*.dll`).
   - Поддержка Windows 7 SP1 x86/x64: компиляция с `/D_WIN32_WINNT=0x0601`, компоновка x86 с `/SUBSYSTEM:CONSOLE,6.01`, x64 с `/SUBSYSTEM:CONSOLE`.
   - Таблица импортов (`dumpbin /dependents`): только базовые системные библиотеки (`KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ADVAPI32.dll`, `OLE32.dll`, `WS2_32.dll`).
   - Нулевая толерантность к предупреждениям компилятора: уровень `/W4` с zero warnings.
3. **Изоляция энкодера от декодера (Encoder-Only Production Linkage):**
   - В релизный бинарник `l4capture.exe` линкуются **только** объектные файлы энкодера OpenH264. Декодер OpenH264 категорически исключается из релизного исполняемого файла, что проверяется через map-файл и таблицу символов.
   - Декодер (`WelsCreateDecoder` / `ISVCDecoder`) линкуется **исключительно** в тестовый раннер `l4capture_tests.exe` для проведения smoke-проверок корректности декодирования сформированного H.264 битового потока.
4. **Управление памятью (Zero-Allocation Invariant):**
   - Контекст энкодера создаётся один раз в `init` и переиспользуется на протяжении всей жизни сессии.
   - Буфер полезной нагрузки Access Unit и массив дескрипторов NAL предвыделяются при инициализации бэкенда (`L4C_MAX_AU_SIZE = 2097152` байт).
   - В горячем цикле `encode()` вызовы `malloc`, `free`, `realloc` **категорически запрещены**.

---

## 3. Pre-Flight Check & Contract Gate (Шаг 1)

Перед началом внесения изменений агент обязан выполнить валидацию входных контрактов:

1. Открой файл журнала `docs\l4capture\prompts\contract-handoff.md`.
2. Убедись, что блок `H-L4C-02-v1` присутствует в секции `## 5. Принятые handoff-блоки` и имеет статус `ACCEPTED`.
3. Сверь контрольные суммы входных артефактов из блока `H-L4C-02-v1`:
   - `docs/l4capture/handoffs/L4C-02-GDI-CAPTURE-report.md`: `3b4b2165fcdc97e081fd09bb6ebf61468ef5ffee4928e1109a81f22ab4b447c0`
   - `tools/l4capture/include/l4capture/gdi_capture.h`: `53f5cd313f456b1f7ff9d494a83d3fe540d1ed95669d089e647d174a85c447ac`
   - `tools/l4capture/include/l4capture/cursor.h`: `c42dc618d95791194f71b3bc6d7b51c13d65019d1bb69ae386d69bf841574e07`
   - `tools/l4capture/include/l4capture/scale.h`: `981404441f4addc6f5462f4ab0695c9eb031afa77c4405fdfcb9d12e3a999d33`
   - `tools/l4capture/include/l4capture/color_convert.h`: `599727122a90e524f9e73db31de2ed2fe36b906dab95799c01e85fe3262d330d`
   - `tools/l4capture/bin/x86/l4capture.exe`: `eb6ab9be19c3139286606dd8170087ea830bb109adb82ea81b6a2d8788e6c3ca`
   - `tools/l4capture/bin/x64/l4capture.exe`: `d5fd979f9524ade34080062c5510a9097f31dfe0b9b0db8b33f4fc96a4f9522f`
4. Проверь рабочую ветку Git: `l4capture/l4c-03-openh264-codec`.
5. При обнаружении несоответствий или повреждений заверши работу со статусом `BLOCKED_CONTRACT`.

---

## 4. Архитектурные требования и техническая спецификация

Реализуй программный кодер H.264 на базе OpenH264 в строгом соответствии с C-интерфейсом `IEncoderBackend` (`include/l4capture/encoder_backend.h`):

### 4.1. OpenH264 Encoder Backend Interface (`include/l4capture/openh264_encoder.h`)

Создай заголовочный файл `include/l4capture/openh264_encoder.h`:

```c
#ifndef L4C_OPENH264_ENCODER_H
#define L4C_OPENH264_ENCODER_H

#include "encoder_backend.h"

/* Фабричный метод создания экземпляра бэкенда OpenH264 */
l4c_status_t l4c_openh264_encoder_create(l4c_encoder_backend_t **out_backend);

#endif /* L4C_OPENH264_ENCODER_H */
```

### 4.2. Конфигурация параметров энкодера OpenH264 (`SEncParamExt`)

При вызове метода `init` виртуальной таблицы `l4c_encoder_backend_vtable_t`:

1. Создаётся экземпляр энкодера через вызов C API OpenH264:
   ```c
   ISVCEncoder *encoder = NULL;
   int rv = WelsCreateSVCEncoder(&encoder);
   if (rv != 0 || !encoder) {
       return L4C_ERR_FATAL;
   }
   ```
2. Запрашиваются параметры по умолчанию и конфигурируются параметры расширенной структуры `SEncParamExt`:
   - `param.iUsageType = CAMERA_VIDEO_REAL_TIME;` (гарантирует совместимость с Constrained Baseline)
   - `param.iPicWidth = config->width;` (`854`)
   - `param.iPicHeight = config->height;` (`480`)
   - `param.iTargetBitrate = (int)(config->target_bitrate_kbps * 1000);` (`500000`)
   - `param.iMaxBitrate = (int)(config->max_bitrate_kbps * 1000);` (`700000`)
   - `param.iRCMode = RC_BITRATE_MODE;`
   - `param.fMaxFrameRate = (float)config->target_fps;` (`10.0f`)
   - `param.iTemporalLayerNum = 1;`
   - `param.iSpatialLayerNum = 1;`
   - Пространственный слой `sSpatialLayers[0]`:
     - `iVideoWidth = config->width;` (`854`)
     - `iVideoHeight = config->height;` (`480`)
     - `fFrameRate = (float)config->target_fps;` (`10.0f`)
     - `iSpatialBitrate = param.iTargetBitrate;`
     - `iMaxSpatialBitrate = param.iMaxBitrate;`
     - `uiProfileIdc = PRO_BASELINE;` (Profile IDC = 66)
     - `uiLevelIdc = LEVEL_3_1;` (Level IDC = 31)
     - `sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;`
   - `param.iComplexityMode = LOW_COMPLEXITY;`
   - `param.uiIntraPeriod = (unsigned int)(config->target_fps * 2);` (`20` кадров = IDR каждые 2.0 секунды)
   - `param.eSpsPpsIdStrategy = CONSTANT_ID;`
   - `param.bEnableFrameSkip = true;`
   - `param.bEnableDenoise = false;`
   - `param.bEnableBackgroundDetection = true;`
   - `param.bEnableAdaptiveQuant = true;`
   - `param.bEnableLongTermReference = false;`
   - `param.iMultipleThreadIdc = 1;`
   - `param.iEntropyCodingModeFlag = 0;` (**CAVLC**, обязателен для Baseline Profile, CABAC в Baseline запрещён)
3. Инициализация параметров:
   ```c
   rv = (*encoder)->InitializeExt(encoder, &param);
   if (rv != 0) {
       WelsDestroySVCEncoder(encoder);
       return L4C_ERR_INVALID_ARG;
   }
   ```
4. Для гарантии генерации SPS/PPS/IDR на первом кадре немедленно взводится флаг форсирования ключевого кадра:
   ```c
   int val = 1;
   (*encoder)->SetOption(encoder, ENCODER_OPTION_IDR_INTERVAL, &param.uiIntraPeriod);
   (*encoder)->ForceIntraFrame(encoder, true);
   ```

### 4.3. Нормализация NAL и формирование Access Unit (`encode`)

При каждом вызове `encode(self, raw, out_au)`:

1. **Валидация формата:** Проверяется `raw->format == L4C_PIX_FMT_I420`.
2. **Проверка таймингов IDR:**
   - Вычисляется время с момента последнего IDR: `now - last_idr_tick_ms`.
   - Если `now - last_idr_tick_ms >= 2000` (прошло $\ge 2.0$ с) ИЛИ взведён флаг `pending_force_idr` ИЛИ `raw->force_idr == true`:
     ```c
     (*encoder)->ForceIntraFrame(encoder, true);
     pending_force_idr = false;
     ```
3. **Подготовка входного кадра `SSourcePicture`:**
   ```c
   SSourcePicture pic;
   memset(&pic, 0, sizeof(pic));
   pic.iColorFormat = videoFormatI420;
   pic.iPicWidth    = (int)raw->width;
   pic.iPicHeight   = (int)raw->height;
   pic.pData[0]     = (uint8_t*)raw->planes[0]; /* Y */
   pic.pData[1]     = (uint8_t*)raw->planes[1]; /* U */
   pic.pData[2]     = (uint8_t*)raw->planes[2]; /* V */
   pic.iStride[0]   = (int)raw->strides[0];
   pic.iStride[1]   = (int)raw->strides[1];
   pic.iStride[2]   = (int)raw->strides[2];
   ```
4. **Кодирование кадра (`EncodeFrame`):**
   ```c
   SFrameBSInfo bs_info;
   memset(&bs_info, 0, sizeof(bs_info));
   int rv = (*encoder)->EncodeFrame(encoder, &pic, &bs_info);
   if (rv != 0) {
       return L4C_ERR_FATAL;
   }
   ```
5. **Обработка пропуска кадра (Frame Skip):**
   - Если `bs_info.eFrameType == videoFrameTypeSkip`:
     - Кадр пропущен rate controller'ом.
     - `out_au->nal_count = 0; out_au->total_bytes = 0;`
     - Возврат `L4C_ERR_NO_FRAME`.
6. **Разбор битового потока и удаление Annex B префиксов (Start Code Stripping):**
   - OpenH264 помещает в `bs_info.sLayerInfo[i].pBsBuf` битовый поток, в котором NAL-пакеты разделены стартовыми кодами Annex B (`0x00000001` (4 байта) или `0x000001` (3 байта)).
   - **ТРЕБОВАНИЕ ДЛЯ RTP (RFC 6184):** RTP-пакетизатор требует **чистую полезную нагрузку NAL без стартовых кодов**.
   - Алгоритм извлечения NAL:
     1. Для каждого слоя `i` от 0 до `bs_info.iLayerNum - 1`:
     2. Использовать массив длин NAL `sLayerInfo[i].pNalLengthInLayer` или сканировать буфер слоя.
     3. Для каждого NAL:
        - Найти смещение стартового кода (3 или 4 байта).
        - Указатель `nal_desc->data` устанавливается на **первый байт после стартового кода** (заголовочный байт NAL).
        - Длина `nal_desc->length` вычисляется как **чистая длина полезной нагрузки без стартового кода**.
        - Тип NAL: `nal_desc->nal_type = nal_desc->data[0] & 0x1F`.
        - Если `nal_desc->nal_type == 5` (IDR) или `bs_info.eFrameType == videoFrameTypeIDR`:
          - `out_au->is_idr = true;`
          - `last_idr_tick_ms = now;`
     4. Если обнаружен IDR, убедиться, что перед NAL типа 5 в массиве дескрипторов присутствуют NAL типа 7 (SPS) и 8 (PPS).
     5. Заполнить выходную структуру `l4c_access_unit_t`:
        - `out_au->nals` = массив дескрипторов (предвыделен в контексте);
        - `out_au->nal_count` = число NAL;
        - `out_au->pts_ms` = `raw->pts_ms`;
        - `out_au->total_bytes` = суммарный объём полезной нагрузки всех NAL;
        - Проверка `out_au->total_bytes <= L4C_MAX_AU_SIZE` (2 МиБ). При превышении — `L4C_ERR_OVERFLOW`.

### 4.4. Коалесцирование Force-IDR (`force_idr`)

Метод `force_idr(self)`:
1. Запрашивает текущее монотонное время `now = l4c_now_monotonic_ms()`.
2. Если `now - last_force_idr_tick_ms < L4C_FORCE_IDR_MIN_INTERVAL_MS` (500 мс):
   - Запрос коалесцируется (игнорируется без ошибки), возврат `L4C_OK`.
3. Иначе:
   - Взводится флаг `pending_force_idr = true`.
   - `last_force_idr_tick_ms = now;`
   - Возврат `L4C_OK`.

### 4.5. Освобождение Access Unit и уничтожение (`release_au`, `destroy`)

- `release_au(self, au)`: Сбрасывает счётчик NAL и флаги в структуре `au`.
- `destroy(self)`:
  - Вызывает `WelsDestroySVCEncoder(encoder);`.
  - Освобождает память буфера AU и контекста бэкенда.

---

## 5. Сборка OpenH264, SBOM и изоляция компоновки

### 5.1. Управление зависимостями и исходными кодами
- Исходный код OpenH264 версии **v2.6.0** расположен в `vendor/openh264/`.
- Версия `v2.6.0` содержит исправление уязвимости `CVE-2025-27091`.
- Запрещено скачивать библиотеки или исходники из сети во время сборки на терминале.

### 5.2. Сборка статической библиотеки (`build.cmd`)
1. OpenH264 компилируется в статическую библиотеку `openh264.lib` (или `openh264_encoder.lib`) с флагом `/MT` для обеих архитектур:
   - x86: компиляция под 32-bit с флагами `/MT /O2 /D_WIN32_WINNT=0x0601`;
   - x64: компиляция под 64-bit с флагами `/MT /O2`.
2. **Изоляция энкодера от декодера (КРИТИЧЕСКОЕ ТРЕБОВАНИЕ):**
   - При сборке исполняемого файла `l4capture.exe` линкуются исключительно модули энкодера (`common` + `processing` + `encoder`).
   - Модули декодера (`decoder`) **НЕ включаются** в `l4capture.exe`.
   - В тестовый раннер `l4capture_tests.exe` включаются как энкодер, так и декодер, что позволяет провести автономную валидацию декодирования битового потока.

### 5.3. Лицензирование и SBOM
- В корень пакета поставляется файл `OPENH264_LICENSE.txt` с лицензией BSD-2-Clause.
- В отчёте `L4C-03-OPENH264-CODEC-report.md` фиксируются точные параметры SBOM:
  - Версия: OpenH264 v2.6.0;
  - Хеш коммита / архива исходников;
  - Параметры компилятора (`/MT`, `/O2`);
  - Предупреждение о патентных обязательствах AVC/H.264 (коммерческое распространение требует отдельного лицензирования MPEG LA / Via LA).

---

## 6. Структура файлов и обновлений в `tools/l4capture/`

```text
tools\l4capture\
├── include\
│   └── l4capture\
│       ├── openh264_encoder.h   # [НОВЫЙ] Интерфейс бэкенда OpenH264
│       └── ... (существующие заголовки)
├── src\
│   ├── encoder\
│   │   └── openh264_encoder.c   # [НОВЫЙ] Реализация IEncoderBackend, SEncParamExt, NAL parsing
│   ├── pipeline\
│   │   └── pipeline.c           # [ОБНОВЛЕНИЕ] Связывание I420 raw_frame -> OpenH264 encoder
│   └── main.c                   # [ОБНОВЛЕНИЕ] Инициализация энкодера, обработка CMD_FORCE_IDR, EVENT_READY
├── tests\
│   ├── test_runner.c            # [ОБНОВЛЕНИЕ] Регистрация тестов кодера
│   └── test_openh264_encoder.c  # [НОВЫЙ] Комплексный тестовый набор энкодера и smoke-декодирования
├── build.cmd                    # [ОБНОВЛЕНИЕ] Сборка/линковка openh264_encoder.c и OpenH264 lib
└── test.cmd                     # [ОБНОВЛЕНИЕ] Сборка и запуск тестового набора кодера
```

---

## 7. Тестовая стратегия и автономная валидация (`tests/`)

Каждый реализованный аспект кодирования обязан быть покрыт строгими модульными тестами в `test_openh264_encoder.c`:

1. **`test_openh264_create_destroy` (Жизненный цикл):**
   - Создание экземпляра через `l4c_openh264_encoder_create()`.
   - Инициализация `vtable->init()` с геометрией 854x480, 10 FPS, 500 кбит/с.
   - Корректное уничтожение `vtable->destroy()` без утечек памяти.
2. **`test_openh264_encode_first_frame_idr` (Ключевой кадр):**
   - Подача синтетического кадра I420 (SMPTE Color Bars).
   - Проверка, что первый закодированный Access Unit содержит:
     - NAL типа 7 (`SPS`);
     - NAL типа 8 (`PPS`);
     - NAL типа 5 (`IDR`);
     - Флаг `out_au.is_idr == true`.
3. **`test_openh264_strip_start_codes` (Удаление стартовых кодов Annex B):**
   - Проверка каждого дескриптора в `out_au.nals`:
     - Первые 3 или 4 байта `nal.data` **НЕ** равны `00 00 01` или `00 00 00 01`.
     - Байт `nal.data[0]` содержит корректный заголовок NAL (`forbidden_zero_bit == 0`, `nal_unit_type == nal.nal_type`).
     - Значение `nal.length` строго равно размеру полезной нагрузки NAL без префикса.
4. **`test_openh264_sps_profile_level` (Совместимость с SDP `42e01f`):**
   - Парсинг NAL типа 7 (SPS):
     - `profile_idc == 66` (`0x42`, Baseline Profile);
     - Флаги ограничений (constraint_set0..3 flags) соответствуют Constrained Baseline (`0xE0`);
     - `level_idc == 31` (`0x1F`, Level 3.1);
     - Размеры закодированного макроблочного растра соответствуют $854 \times 480$ (с учётом frame cropping).
5. **`test_openh264_periodic_idr_cadence` (Каденция 2.0 секунды):**
   - Кодирование последовательности из 30 кадров с шагом `pts_ms += 100` (эмуляция 10 FPS).
   - Кадр 0: IDR (t = 0 мс).
   - Кадры 1..19: non-IDR (P-кадры, NAL типа 1, `is_idr == false`).
   - Кадр 20 (t = 2000 мс): генерация IDR (SPS + PPS + IDR, `is_idr == true`).
6. **`test_openh264_force_idr_coalescing` (Коалесцирование force_idr):**
   - Вызов `force_idr()` на кадре 5 (t = 500 мс) $\to$ кадр 6 генерирует IDR.
   - Повторный вызов `force_idr()` через 100 мс (t = 600 мс) $\to$ запрос отбрасывается (coalesced).
   - Вызов `force_idr()` через 550 мс (t = 1150 мс) $\to$ запрос принимается и генерирует IDR.
7. **`test_openh264_frame_skip_handling` (Пропуск кадров):**
   - Проверка корректного поведения при пропуске кадра энкодером (`videoFrameTypeSkip`): возврат `L4C_ERR_NO_FRAME`, нулевое число NAL в AU, отсутствие сбоев в последующих кадрах.
8. **`test_openh264_decoder_smoke` (Эталонное декодирование битового потока):**
   - Инициализация декодера OpenH264 (`WelsCreateDecoder` / `ISVCDecoder`).
   - Добавление стартовых кодов Annex B к NAL из закодированного AU и подача в декодер.
   - Проверка успешного декодирования (`rv == 0`).
   - Проверка размеров декодированного буфера: ширина $854$, высота $480$.
9. **`test_openh264_memory_soak` (Стабильность памяти):**
   - Кодирование 100 последовательных кадров I420.
   - Замер памяти через `GetProcessMemoryInfo` (Private Bytes).
   - Подтверждение отсутствия монотонного роста памяти после первых 5 кадров прогрева.
10. **Проверка зависимостей и платформы (`dumpbin`):**
    - `dumpbin /dependents bin\x86\l4capture.exe` — только системные Win32 DLL (`KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ADVAPI32.dll`, `OLE32.dll`, `WS2_32.dll`).
    - Полное отсутствие `MSVCR*.dll`, `VCRUNTIME*.dll`.
    - Подсистема x86: Console 6.01.

---

## 8. Пошаговый алгоритм выполнения агентом (Agent Workflow)

Агент выполняет реализацию строго по следующим этапам:

1. **Этап 1: Contract Gate & Контроль допуска**
   - Проверить `docs/l4capture/prompts/contract-handoff.md` на наличие блока `H-L4C-02-v1` со статусом `ACCEPTED`.
   - Проверить совпадение контрольных сумм всех входных файлов.
2. **Этап 2: Подготовка и сборка библиотеки OpenH264**
   - Проверить наличие исходных файлов OpenH264 v2.6.0 в `vendor/openh264/`.
   - Обеспечить сборку статической библиотеки OpenH264 с флагом `/MT` для x86 и x64.
3. **Этап 3: Заголовочный файл бэкенда**
   - Создать `include/l4capture/openh264_encoder.h`.
4. **Этап 4: Реализация OpenH264 бэкенда**
   - Реализовать `src/encoder/openh264_encoder.c` с полным жизненным циклом `IEncoderBackend`:
     - Инициализация `SEncParamExt` (854x480, 10 FPS, 500/700 kbps, CAVLC, Constrained Baseline 3.1).
     - Разбор `SFrameBSInfo`, стриппинг стартовых кодов Annex B, заполнение `l4c_nal_desc_t`.
     - Каденция IDR (первый кадр, periodic 2000 мс, force-IDR coalescing 500 мс).
     - Обработка `videoFrameTypeSkip`.
5. **Этап 5: Интеграция с конвейером и `main.c`**
   - Обновить `src/pipeline/pipeline.c` для передачи сырых кадров I420 из `color_convert` в `openh264_encoder`.
   - Обновить `src/main.c`: приём команды `CMD_FORCE_IDR`, генерация `EVENT_READY` при отправке первого IDR-кадра.
6. **Этап 6: Написание тестового набора**
   - Реализовать `tests/test_openh264_encoder.c` с полным покрытием (create/destroy, first IDR, NAL stripping, SPS profile/level, periodic IDR, force-IDR coalescing, decoder smoke, memory soak).
   - Зарегистрировать тесты в `tests/test_runner.c`.
7. **Этап 7: Сборка и сквозной прогон тестов**
   - Обновить `build.cmd` и `test.cmd`.
   - Собрать Release-версии x86 и x64 (`build.cmd all`). Убедиться в полном отсутствии предупреждений (/W4 Zero Warnings).
   - Запустить весь тестовый набор (`test.cmd`). Убедиться в 100% успехе (0 failures).
   - Проверить таблицу импортов через `dumpbin /dependents`.
   - Убедиться в отсутствии символов декодера в релизном `l4capture.exe` через `dumpbin /symbols`.
8. **Этап 8: Подготовка отчёта и Candidate Handoff**
   - Сформировать отчёт `docs/l4capture/handoffs/L4C-03-OPENH264-CODEC-report.md`.
   - Рассчитать SHA-256 реальных байтов отчёта.
   - Сформировать кандидат `docs/l4capture/handoffs/L4C-03-OPENH264-CODEC-candidate.md` в формате `DETACHED_V1`.

---

## 9. Критерии приёмки (Definition of Done)

Шаг считается завершенным только при одновременном выполнении следующих условий:

1. **Компиляция и сборка:**
   - Бинарники `bin\x86\l4capture.exe` и `bin\x64\l4capture.exe` успешно собираются через `build.cmd` с флагом `/MT`.
   - `bin\l4capture.exe` идентичен `bin\x86\l4capture.exe`.
   - Компиляция проходит без единого предупреждения при уровне `/W4`.
   - Таблица зависимостей (`dumpbin /dependents`): только системные DLL, отсутствие внешних VC Runtime библиотек.
2. **Изоляция энкодера (Encoder-Only Linkage):**
   - В релизном исполняемом файле `l4capture.exe` отсутствуют символы декодера OpenH264 (`WelsCreateDecoder`, `WelsDestroyDecoder`).
3. **Соответствие профилю H.264:**
   - Первый кадр обязательно содержит SPS (7), PPS (8) и IDR (5).
   - SPS соответствует Constrained Baseline Profile (`profile_idc = 66`), Level 3.1 (`level_idc = 31`), совместим с SDP `42e01f`.
4. **Нормализация NAL:**
   - Все NAL в Access Unit не содержат стартовых кодов Annex B (`00 00 00 01` / `00 00 01`), указатели `data` указывают на NAL header byte.
5. **Каденция IDR и тайминги:**
   - Периодический IDR генерируется не реже чем каждые 2.0 секунды.
   - Вызовы `force_idr()` коалесцируются с интервалом $\le 500$ мс.
6. **Декодирование и валидация видеопотока:**
   - Эталонный декодер OpenH264 успешно декодирует закодированный поток в растр $854 \times 480$ без ошибок.
7. **Тестовое покрытие и память:**
   - Все тесты в `tests/` выполняются успешно через `test.cmd` (0 failures, 0 errors).
   - 100-кадровый тест подтверждает стабильность памяти (zero growth).
8. **Оформление handoff:**
   - Файлы `l4desk-service` не затронуты.
   - Отчёт и кандидат подготовлены строго в каталоге `docs/l4capture/handoffs/`.

---

## 10. Оформление отчёта и Candidate Handoff (`DETACHED_V1`)

По завершении всех работ сформируй два артефакта согласно стандарту `PROMPT-STANDARD.md`:

### 1. Отчёт исполнителя: `docs/l4capture/handoffs/L4C-03-OPENH264-CODEC-report.md`
Должен содержать:
- Статус: строго `ACCEPTED`.
- Ветка и SHA коммита реализации (`producer_commit`).
- Полный перечень созданных и изменённых файлов в `tools/l4capture/`.
- Вывод выполнения `build.cmd all` и `test.cmd`.
- Результаты верификации профиля H.264 (SPS/PPS), NAL-стриппинга и каденции IDR.
- Результаты проверки эталонного декодирования (`ISVCDecoder`).
- Вывод `dumpbin /dependents` для x86 и x64.
- Подтверждение отсутствия символов декодера в релизном бинарнике (`dumpbin`).
- Раздел SBOM: версия OpenH264 v2.6.0, хеши исходников/библиотеки, параметры сборки, патентное уведомление AVC.

### 2. Файл кандидата: `docs/l4capture/handoffs/L4C-03-OPENH264-CODEC-candidate.md`
Вычисли SHA-256 хеш реальных байтов отчёта (PowerShell: `(Get-FileHash -Algorithm SHA256 docs\l4capture\handoffs\L4C-03-OPENH264-CODEC-report.md).Hash.ToLower()`).

Файл кандидата оформляется строго в формате:

```markdown
<!-- HANDOFF:H-L4C-03-v1:BEGIN -->
```yaml
handoff_id: H-L4C-03-v1
status: ACCEPTED
contract_kinds:
  - OPENH264_CODEC
  - I420_ENCODER
  - AVC_BASELINE_3_1
  - NAL_NORMALIZATION
  - IDR_CADENCE
  - ENCODER_ONLY_LINKAGE
producer_prompt_id: L4C-03-OPENH264-CODEC
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-03-OPENH264-CODEC-report.md
producer_branch: l4capture/l4c-03-openh264-codec
producer_commit: <GIT_COMMIT_SHA>
accepted_at_utc: <ISO_8601_TIMESTAMP>
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-03-OPENH264-CODEC-report.md
  - tools/l4capture/include/l4capture/openh264_encoder.h
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - <SHA256_REPORT>
  - <SHA256_OPENH264_ENCODER_H>
  - <SHA256_BIN_X86_EXE>
  - <SHA256_BIN_X64_EXE>
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
  breaking_changes: false
  notes: Реализация программного видеоэнкодера OpenH264 v2.6.0 через C API. Constrained Baseline Level 3.1 (SDP 42e01f), нормализация NAL без Annex B префиксов, каденция IDR (периодическая 2.0 с, force-IDR 500 мс), encoder-only линковка.
deployment_status: LOCAL_BUILD_VERIFIED
deployed_environment: local_development
feature_flags:
  l4capture_native_pipeline: enabled
  l4capture_openh264_encoder: enabled
contract_payload:
  encoder_backend:
    library: OpenH264 v2.6.0 (CVE-2025-27091 fix)
    linkage: static (/MT, encoder-only in production binary)
    c_interface: IEncoderBackend (init, encode, force_idr, release_au, destroy)
    input_format: L4C_PIX_FMT_I420 (planar Y, U, V)
    output_format: AccessUnit (l4c_access_unit_t with l4c_nal_desc_t array)
  h264_profile:
    profile: Constrained Baseline (PRO_BASELINE, profile_idc=66)
    level: Level 3.1 (LEVEL_3_1, level_idc=31)
    sdp_compatibility: 42e01f
    entropy_coding: CAVLC (iEntropyCodingModeFlag=0)
    spatial_layers: 1 (single layer, no SVC)
    b_frames: forbidden (none)
    lookahead: none
  rate_control:
    mode: RC_BITRATE_MODE
    target_raster: 854x480 (base_480p)
    target_fps: 10
    target_bitrate_kbps: 500
    max_bitrate_kbps: 700
    frame_skip: enabled (handled as non-fatal encoder_drops)
  nal_and_au:
    start_code_stripping: true (Annex B 00 00 01 / 00 00 00 01 stripped, raw NAL payload)
    max_au_size: 2097152 (2 MB cap)
    first_frame_structure: SPS (7) + PPS (8) + IDR (5)
  keyframe_cadence:
    periodic_idr_interval_ms: 2000 (at least once every 2.0 s monotonic time)
    sps_pps_repeat: before each IDR
    force_idr_coalescing_ms: 500 (max 1 unscheduled IDR per 500 ms)
  supply_chain_and_licensing:
    version: v2.6.0
    cve_fixed: CVE-2025-27091
    license: BSD-2-Clause
    avc_patent_notice: Commercial distribution requires explicit AVC patent authorization
supersedes: []
known_risks:
  - R2: Нарушение доставки/late join — периодический IDR каждые 2.0 с обеспечивает восстановление декодирования без зависимости от PLI
  - R3: Внутренняя перегрузка — frame skip не крашит процесс, предвыделенные буферы AU (2 МБ) исключают malloc в encode
  - R4: Несовместимость формата/SPS — строгий Constrained Baseline Level 3.1 гарантирует декодирование браузером WebRTC
consumers:
  - L4C-04-RTP-SENDER
  - L4C-05-AGENT-ADAPTER
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
next_prompt_id: L4C-04-RTP-SENDER
```
<!-- HANDOFF:H-L4C-03-v1:END -->
```
