# L4C-08-MF-ENCODER — Аппаратное кодирование H.264 через Windows Media Foundation MFT, динамическая загрузка, обработка NV12 и безопасный OpenH264 fallback

```yaml
prompt_id: L4C-08-MF-ENCODER
scope_project: tools/l4capture
scope_root: D:\repo\platerra\Public\etranprocessing\tools\l4capture
prompt_type: implementation-step
required_handoff_ids:
  - H-L4C-07-v1
sequence_gate_status: READY_FOR_L4C_08
output_handoff_id: H-L4C-08-v1
next_prompt_id: L4C-09-PROFILES-DEGRADE
branch: l4capture/l4c-08-mf-encoder
report_path: docs/l4capture/handoffs/L4C-08-MF-ENCODER-report.md
candidate_format: DETACHED_V1
candidate_path: docs/l4capture/handoffs/L4C-08-MF-ENCODER-candidate.md
architecture_sections: [1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 13, 14]
consumers:
  - L4C-09-PROFILES-DEGRADE
  - L4C-10-WIN7-TELEMETRY
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
```

---

## 1. Цель и архитектурная миссия

Ты выступаешь в роли **Ведущего системного инженера аппаратного видеокодирования Windows и разработчика медиакодеков (L4Capture-MFEncoder-Agent)** в рамках комплекса `tools suite`.

Твоя задача — реализовать высокопроизводительный нативный модуль аппаратного сжатия видеопотока H.264 на базе технологии **Windows Media Foundation Transform (Hardware MFT)** через чистый C API (COM C-интерфейсы `lpVtbl`) в целевой директории `tools\l4capture\`.

### Архитектурный контекст и системная ценность

На шагах Фазы 1 (L4C-01 .. L4C-06) был создан и верифицирован базовый сквозной тракт захвата на базе GDI и программного кодера OpenH264 (контрольная веха M-1 принята с вердиктом «НОРМ»). На шаге L4C-07 был успешно внедрён аппаратный захват экрана через DXGI Desktop Duplication, что позволило существенно разгрузить графическую подсистему на современных версиях Windows.

Однако при программном кодировании H.264 через OpenH264 центральный процессор платёжного терминала расходует до 20–25% вычислительной мощности двухъядерного CPU на сжатие кадрового потока. На терминалах самообслуживания процессорное время является критически ограниченным ресурсом: избыточная загрузка CPU видеокодером повышает риск задержек в обработке транзакций, ответа купюроприёмников, фискальных регистраторов и отрисовке пользовательского интерфейса.

Внедрение аппаратного кодирования **L4C-08-MF-ENCODER** решает эту проблему за счёт переноса ресурсоёмких математических операций сжатия (дискретное косинусное преобразование, оценка движения, энтропийное кодирование) на выделенные аппаратные ASIC-блоки GPU (Intel QuickSync Video, NVIDIA NVENC, AMD AMF):
- Нагрузка на CPU снижается с 20–25% до $\le 5\%$ на современных процессорах;
- Сокращается тепловыделение и энергопотребление оборудования в закрытых корпусах терминалов;
- Полностью высвобождаются вычислительные ядра для непрерывной и надёжной обработки платёжных операций;
- При отсутствии совместимого аппаратного ускорителя или сбое в работе GPU сохраняется 100% надёжность сервиса благодаря мгновенному и прозрачному переходу на гарантированный программный бэкенд OpenH264.

---

## 2. Непереговорные рамочные принципы и изоляция

1. **Строгая изоляция директорий (Directory Boundary):**
   - **Код, заголовки, сборка и тесты:** размещаются строго в `tools\l4capture\`.
   - **Документация, промпты, журнал контрактов:** находятся в `docs\l4capture\`.
   - **Отчёты и candidate-файлы:** формируются строго в `docs\l4capture\handoffs\`.
   - **КАТЕГОРИЧЕСКИЙ ЗАПРЕТ `l4desk-service`:** Строжайше запрещено создавать, изменять или использовать файлы внутри каталога `l4desk-service`. Проект `l4capture` полностью автономен.
   - **ЗАПРЕТ на модификацию других подсистем:** Запрещено изменять файлы в `BACK\`, `FRONT\`, `tools\l4desk`, `ProcessingBackend\`, `MenuBuilder\`, `shared\`, `sqlFileExample\`, `stored-procedures\`.
2. **Стандарты чистого C и компиляции:**
   - Код пишется строго на **C99/C11** (подмножество MSVC, компиляция через `cl.exe`, не C++). Взаимодействие с COM-интерфейсами Media Foundation (`IMFTransform`, `IMFMediaType`, `IMFSample`, `IMFMediaBuffer`, `ICodecAPI`) осуществляется исключительно через C-таблицы функций (`lpVtbl`).
   - Статическая компоновка Runtime: обязательный флаг `/MT` для Release-конфигурации (никаких динамических зависимостей `msvcrt*.dll`, `vcruntime*.dll`).
   - Поддержка Windows 7 SP1 x86/x64: компиляция с `/D_WIN32_WINNT=0x0601`, компоновка x86 с `/SUBSYSTEM:CONSOLE,6.01`, x64 с `/SUBSYSTEM:CONSOLE`.
   - Нулевая толерантность к предупреждениям компилятора: уровень `/W4` с zero warnings (`/WX`).
3. **Безопасная динамическая загрузка Media Foundation (System Directory Only):**
   - Согласно §3 архитектуры, библиотеки Media Foundation (`mfplat.dll`, `mf.dll`) должны загружаться **строго из системного каталога** (`LoadLibraryExW(..., NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)`).
   - **Строгий запрет статической линковки к `mfplat.lib`, `mf.lib`, `mfuuid.lib`:** бинарник `l4capture.exe` обязан беспрепятственно запускаться на любых версиях Windows, включая минимальные сборки Windows 7 SP1 и Windows Embedded POSReady, где Media Foundation может отсутствовать или быть урезанным. Проверка чистоты импортов через `dumpbin /imports` обязательна.
   - Все необходимые GUID (`CLSID_CMSH264EncoderMFT`, `MFT_CATEGORY_VIDEO_ENCODER`, `MFVideoFormat_H264`, `MFVideoFormat_NV12`, `MF_MT_MAJOR_TYPE`, `MF_MT_SUBTYPE`, `IID_IMFTransform`, `IID_ICodecAPI` и связанные свойства) определяются локально в виде констант в C-коде.
4. **Ресурсная дисциплина и Zero-Allocation Invariant:**
   - Предварительное выделение буферов семплов (`IMFSample`), медиа-буферов (`IMFMediaBuffer`) и буфера выходного Access Unit (`L4C_MAX_AU_SIZE = 2097152` байт) производится однократно в `init()`.
   - В горячем цикле `encode()` вызовы `malloc`, `free`, `realloc`, а также динамическое создание новых COM-объектов **категорически запрещены**.
   - Обязательное освобождение всех промежуточных COM-объектов (`Release()`) в ветках ошибок и в `destroy()`.
   - Корректный вызов `MFShutdown()` при выгрузке бэкенда.
5. **Ограниченное зондирование (Bounded Probe $\le 2.0$ с) и безопасный Fallback:**
   - Согласно §5.4 и §13.2 архитектуры, зондирование аппаратного MFT ограничено жестким тайм-аутом **2.0 секунды** и входит в общий бюджет старта сервиса.
   - Зависший вызов в драйвере GPU не должен приводить к зависанию watchdog или нарушению дедлайна аренды (lease deadline).
   - Несовместимый, зависший или сбойный аппаратный MFT влечёт ровно **один контролируемый переход на программный бэкенд OpenH264** при действующей аренде.

---

## 3. Pre-Flight Check & Contract Gate (Шаг 1)

Перед началом внесения изменений агент обязан выполнить валидацию входных контрактов:

1. Открой файл журнала `docs\l4capture\prompts\contract-handoff.md`.
2. Убедись, что блок `H-L4C-07-v1` присутствует в секции `## 5. Принятые handoff-блоки` и имеет статус `ACCEPTED`.
3. Сверь контрольные суммы входных артефактов из блока `H-L4C-07-v1`:
   - `docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-report.md`: `da782bede4e7e2637434301596e2ba62024216ed6a5de04df4fdecea271c4a56`
   - `tools/l4capture/include/l4capture/dxgi_capture.h`: `cc2df472c09f65644343796263d21fff2bca7317d307ba7f824786add4eb369c`
   - `tools/l4capture/src/capture/dxgi_capture.c`: `4e0bef6f694c28839052194b0c68ed32004faccd0b69f8883f3aec60873f467c`
   - `tools/l4capture/bin/x86/l4capture.exe`: `1a689e3c274590fcefa63e1b0ab737dc874880ab21529910ec312892ca422319`
   - `tools/l4capture/bin/x64/l4capture.exe`: `76a7b04ffb6c9e54ec767b813aa1efa0f87a36cc3fafbb3d42e854c25e7fd45a`
4. Проверь статус шага 8 в таблице реестра `contract-handoff.md`: `READY_FOR_L4C_08` («Готов к запуску»).
5. Проверь рабочую ветку Git: `l4capture/l4c-08-mf-encoder`.
6. При обнаружении несоответствий или повреждений заверши работу со статусом `BLOCKED_CONTRACT`.

---

## 4. Архитектурные требования и техническая спецификация

Реализуй бэкенд аппаратного кодирования Windows Media Foundation в строгом соответствии с C-интерфейсом `l4c_encoder_backend_t` (`include/l4capture/encoder_backend.h`):

```c
typedef struct l4c_encoder_backend_vtable {
    l4c_status_t (*init)(struct l4c_encoder_backend *self, const l4c_encoder_config_t *config);
    l4c_status_t (*encode)(struct l4c_encoder_backend *self, const l4c_raw_frame_t *raw, l4c_access_unit_t *out_au);
    l4c_status_t (*force_idr)(struct l4c_encoder_backend *self);
    void (*release_au)(struct l4c_encoder_backend *self, l4c_access_unit_t *au);
    void (*destroy)(struct l4c_encoder_backend *self);
} l4c_encoder_backend_vtable_t;
```

### 4.1. Публичный заголовочный интерфейс MF Encoder (`include/l4capture/mf_encoder.h`)

Создай заголовочный файл `include/l4capture/mf_encoder.h`:

```c
#ifndef L4C_MF_ENCODER_H
#define L4C_MF_ENCODER_H

#include "encoder_backend.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Проверка доступности и работоспособности аппаратного H.264 MFT энкодера.
 * Выполняет безопасное зондирование (probe) за время <= 2.0 секунд.
 * Возвращает true только при наличии подтверждённого аппаратного энкодера GPU
 * (Intel QSV, NVIDIA NVENC, AMD AMF), поддерживающего входной формат NV12
 * и профиль H.264 Constrained Baseline / Level 3.1.
 * Возвращает false на Windows 7, виртуальных машинах без GPU или при сбое MFT.
 */
bool l4c_mf_encoder_is_supported(void);

/*
 * Фабричный метод создания экземпляра бэкенда аппаратного кодирования MF.
 * Выделяет структуру бэкенда и привязывает виртуальную таблицу функций.
 */
l4c_status_t l4c_mf_encoder_create(l4c_encoder_backend_t **out_backend);

#ifdef __cplusplus
}
#endif

#endif /* L4C_MF_ENCODER_H */
```

### 4.2. Безопасный динамический загрузчик Media Foundation (Win7 SP1 Compatibility)

В файле `src/encoder/mf_encoder.c` реализуй механизм изолированной динамической загрузки Media Foundation API:
1. Загрузка библиотек строго из системного каталога `System32`:
   ```c
   HMODULE hMfPlat = LoadLibraryExW(L"mfplat.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
   HMODULE hMf = LoadLibraryExW(L"mf.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
   ```
2. Разрешение указателей на функции:
   - `MFStartup` (`HRESULT (WINAPI *)(ULONG, DWORD)`)
   - `MFShutdown` (`HRESULT (WINAPI *)(void)`)
   - `MFTEnumEx` (`HRESULT (WINAPI *)(GUID, UINT32, const MFT_REGISTER_TYPE_INFO*, const MFT_REGISTER_TYPE_INFO*, IMFActivate***, UINT32*)`)
   - `MFCreateMediaType` (`HRESULT (WINAPI *)(IMFMediaType**)`)
   - `MFCreateSample` (`HRESULT (WINAPI *)(IMFSample**)`)
   - `MFCreateMemoryBuffer` (`HRESULT (WINAPI *)(DWORD, IMFMediaBuffer**)`)
   - `MFCreateAttributes` (`HRESULT (WINAPI *)(IMFAttributes**, UINT32)`)
3. Автономные определения GUID в C-коде (без статической привязки к `mfuuid.lib`):
   - `CLSID_CMSH264EncoderMFT`, `MFT_CATEGORY_VIDEO_ENCODER`
   - `MFMediaType_Video`, `MFVideoFormat_H264`, `MFVideoFormat_NV12`
   - `MF_MT_MAJOR_TYPE`, `MF_MT_SUBTYPE`, `MF_MT_FRAME_SIZE`, `MF_MT_FRAME_RATE`, `MF_MT_PIXEL_ASPECT_RATIO`, `MF_MT_INTERLACE_MODE`, `MF_MT_AVG_BITRATE`, `MF_MT_MPEG2_PROFILE`, `MF_MT_MPEG2_LEVEL`
   - `IID_IMFTransform`, `IID_ICodecAPI`
   - `CODECAPI_AVEncCommonRateControlMode`, `CODECAPI_AVEncCommonMeanBitRate`, `CODECAPI_AVEncCommonMaxBitRate`, `CODECAPI_AVEncCommonLowLatency`, `CODECAPI_AVEncMPVDefaultBPictureCount`, `CODECAPI_AVEncMPVGOPSize`, `CODECAPI_AVEncH264CABACEnable`, `CODECAPI_AVEncVideoForceKeyFrame`
4. Если `mfplat.dll` или `mf.dll` не найдены, либо ключевые экспорты отсутствуют: `l4c_mf_encoder_is_supported()` возвращает `false`, а вызовы создания возвращают отказ без сбоя процесса.

### 4.3. Зондирование оборудования (Hardware MFT Probe $\le 2.0$ с)

Согласно §5.4 и §12.3:
1. **Строгая фильтрация по признаку `MFT_ENUM_FLAG_HARDWARE`:**
   - Вызов `MFTEnumEx` с категорией `MFT_CATEGORY_VIDEO_ENCODER`, флагами `MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER`.
   - Входной формат: `MFMediaType_Video`, подтип `MFVideoFormat_NV12`.
   - Выходной формат: `MFMediaType_Video`, подтип `MFVideoFormat_H264`.
2. **Проверка реальной работоспособности (No Fake Hardware Pass):**
   - Наличие строк драйвера GPU («Intel», «NVIDIA», «AMD») или устройств в системе **не является доказательством работоспособности MFT**.
   - Активация `IMFTransform` через `IMFActivate::ActivateObject`.
   - Пробная конфигурация входного типа `NV12` и выходного типа `H.264`.
   - Немедленное корректное освобождение пробного MFT (`Release`) и деактивация активаторов.
   - Ограничение по времени выполнения зондирования: тайм-аут $\le 2.0$ секунды. При превышении тайм-аута — возврат `false` и фиксация причины `L4C_FALLBACK_MFT_UNAVAILABLE`.

### 4.4. Параметры кодирования и совместимость с SDP 42e01f (§2, §5.4, §10)

1. **Профиль и уровень H.264:**
   - **Constrained Baseline Profile** (`eAVEncH264VProfile_Base` / `profile_idc = 66`), Level 3.1 (`eAVEncH264VLevel3_1` / `level_idc = 31`), строго совместимый с SDP параметром `profile-level-id=42e01f;packetization-mode=1`.
   - Один пространственный слой (single-layer AVC), запрет SVC.
2. **Запрет B-кадров и минимизация задержки:**
   - `CODECAPI_AVEncMPVDefaultBPictureCount = 0` (строго без B-кадров, только I- и P-кадры).
   - `CODECAPI_AVEncCommonLowLatency = VARIANT_TRUE` (отключение буферизации lookahead и reorder).
   - Энтропийное кодирование CAVLC: `CODECAPI_AVEncH264CABACEnable = VARIANT_FALSE` (где поддерживается драйвером).
3. **Управление битрейтом (Rate Control):**
   - Режим управления: CBR или ограниченный VBR (`eAVEncCommonRateControlMode_CBR` или `eAVEncCommonRateControlMode_UnconstrainedVBR`).
   - `CODECAPI_AVEncCommonMeanBitRate` = `config->target_bitrate_kbps * 1000` (500 кбит/с для профиля 480p).
   - `CODECAPI_AVEncCommonMaxBitRate` = `config->max_bitrate_kbps * 1000` (700 кбит/с для профиля 480p).
4. **Каденция ключевых кадров и GOP:**
   - `CODECAPI_AVEncMPVGOPSize = config->target_fps * 2` (генерация IDR не реже 1 раза в 2.0 секунды).
   - Первый закодированный кадр **ОБЯЗАН** быть полным IDR-кадром с SPS и PPS.

### 4.5. Цветовая конверсия BGRA $\to$ NV12 (`src/pipeline/color_convert.c`)

Согласно §5.3 архитектуры: MFT принимает формат **NV12**, в то время как OpenH264 принимает **I420**. Скрытая конверсия NV12 $\to$ I420 на каждом тике запрещена.
1. В `include/l4capture/color_convert.h` и `src/pipeline/color_convert.c` добавь специализированную функцию:
   ```c
   l4c_status_t l4c_color_convert_bgra_to_nv12(
       l4c_color_converter_t *converter,
       const uint8_t *src_bgra,
       int32_t src_stride,
       uint8_t *dst_y,
       int32_t dst_stride_y,
       uint8_t *dst_uv,
       int32_t dst_stride_uv,
       uint32_t width,
       uint32_t height
   );
   ```
2. **Математическая модель конверсии:**
   - Стандарт: **ITU-R BT.601 Limited Range** (тот же, что и для I420):
     $$Y = ((66 \cdot R + 129 \cdot G + 25 \cdot B + 128) \gg 8) + 16$$
     $$U = ((-38 \cdot R - 74 \cdot G + 112 \cdot B + 128) \gg 8) + 128$$
     $$V = ((112 \cdot R - 94 \cdot G - 18 \cdot B + 128) \gg 8) + 128$$
   - Насыщение (clamping): $Y \in [16, 235]$, $U, V \in [16, 240]$.
   - Субдискретизация цветности: $2 \times 2$ усреднение цветовых компонент для блоков $2 \times 2$ пикселей.
   - Компоновка NV12: плоскость $Y$ размером $W \times H$ байт; плоскость $UV$ размером $W \times (H / 2)$ байт с чередованием байт $U$ и $V$ (`U0, V0, U1, V1, ...`).

### 4.6. Цикл обработки кадров MFT и Zero-Allocation (§5.1, §13.2)

1. **Предварительное выделение буферов (`init`):**
   - Вычисление размера входного буфера NV12: $Y\_size + UV\_size = W \cdot H + W \cdot (H / 2)$.
   - Создание входного семпла `MFCreateSample` и входного медиа-буфера `MFCreateMemoryBuffer`.
   - Создание выходного семпла `MFCreateSample` и выходного медиа-буфера размером не менее `pOutputInfo.cbSize` (или дефолтный `L4C_MAX_AU_SIZE`).
   - Предварительное выделение памяти под дескрипторы NAL (`l4c_nal_desc_t[64]`) и внутренний буфер AU (`L4C_MAX_AU_SIZE = 2097152` байт) в структуре бэкенда.
2. **Подача входного кадра (`encode`):**
   - Блокировка входного буфера: `pInputBuffer->lpVtbl->Lock(..., &pData, NULL, NULL)`.
   - Прямое копирование данных из `l4c_raw_frame_t` (плоскость Y и плоскость UV) с учётом страйдов.
   - Разблокировка входного буфера: `pInputBuffer->lpVtbl->Unlock(...)`.
   - Установка временной метки семпла (в единицах 100 нс):
     `pInputSample->lpVtbl->SetSampleTime(pInputSample, raw->pts_ms * 10000)`.
   - Установка длительности семпла: `pInputSample->lpVtbl->SetSampleDuration(pInputSample, (1000 / config->target_fps) * 10000)`.
   - Подача в MFT: `pTransform->lpVtbl->ProcessInput(pTransform, 0, pInputSample, 0)`.
3. **Извлечение выходного Access Unit (`ProcessOutput`):**
   - Подготовка структуры `MFT_OUTPUT_DATA_BUFFER`.
   - Вызов `pTransform->lpVtbl->ProcessOutput(pTransform, 0, 1, &outputBuffer, &status)`.
   - Обработка возвращаемых кодов:
     - `MF_E_TRANSFORM_NEED_MORE_INPUT`: MFT требует ещё один входной кадр (нормальное состояние, возвращается `L4C_ERR_NO_FRAME`).
     - `MF_E_TRANSFORM_STREAM_CHANGE`: динамическое изменение формата выходного потока (обновление выходного типа через `SetOutputType`).
     - `S_OK`: закодированный кадр готов к разбору.

### 4.7. Нормализация NAL, удаление префиксов и кадрирование Access Unit (§5.4, §10)

1. **Нормализация битового потока (Annex B / AVCC Stripping):**
   - Выходной поток аппаратного MFT может содержать стартовые коды Annex B (`00 00 00 01` или `00 00 01`) либо AVCC-префиксы длины.
   - Выполняется разбор выходного буфера, **полное удаление всех стартовых кодов и префиксов длины**.
   - Для каждого NAL формируется дескриптор `l4c_nal_desc_t`:
     - `data` указывает непосредственно на первый байт заголовка NAL (NAL header byte);
     - `length` содержит точную длину полезной нагрузки NAL в байтах;
     - `nal_type` извлекается из первых 5 бит заголовка (`header & 0x1F`).
2. **Инвариант первого кадра и повторение SPS/PPS:**
   - В самом первом закодированном Access Unit обязательно присутствуют NAL типов 7 (SPS), 8 (PPS) и 5 (IDR).
   - Если аппаратный MFT выдаёт SPS/PPS отдельно от слайса IDR, они объединяются в единый выходной Access Unit `l4c_access_unit_t`.
   - **Обязательное повторение актуальных SPS/PPS перед каждым последующим IDR-кадром (§5.4, §10):** если драйвер GPU не сгенерировал SPS/PPS перед IDR, бэкенд вставляет сохранённые актуальные SPS/PPS в массив NAL данного AU.
3. **Обработка паддинга макроблоков (854x480 Cropping Check, §5.3):**
   - Разрешение `854x480` не кратно 16 по горизонтали ($854 \div 16 = 53.375$, ближайшее кратное 16 — 864).
   - При кодировании $854 \times 480$ аппаратный кодер дополняет строку до 864 пикселей.
   - Бэкенд обязан проверить поля кадрирования (frame cropping) в заголовке SPS (`frame_cropping_flag`, `frame_crop_right_offset`), гарантируя, что видимая декодируемая область остаётся строго **854 пикселя**, без искажений пропорций и черных полос.

### 4.8. Коалесцирование запросов ключевого кадра (Force-IDR)

1. При вызове метода `force_idr(self)`:
   - Проверяется интервал от момента последней генерации IDR: `now_ms - last_idr_ms`.
   - Если прошло менее 500 мс (`L4C_FORCE_IDR_MIN_INTERVAL_MS = 500`), запрос объединяется (coalesced) и отбрасывается.
   - Если прошло $\ge 500$ мс: через интерфейс `ICodecAPI` выставляется свойство `CODECAPI_AVEncVideoForceKeyFrame = VARIANT_TRUE`.
2. Начальный старт потока и смена разрешения не ограничиваются интервалом 500 мс.

### 4.9. Безопасный переход на OpenH264 (Controlled Fallback)

1. Если зондирование `l4c_mf_encoder_is_supported()` вернуло `false`, инициализация MFT завершилась с ошибкой, либо MFT вернул невосстановимую ошибку в цикле кодирования:
   - Выполняется ровно **один контролируемый переход на программный бэкенд OpenH264** (`l4c_openh264_encoder_create`).
   - Конвейер автоматически переключает преобразование цвета на формат **I420** (`l4c_color_convert_bgra_to_i420`).
   - Фиксируется диагностическая причина: `L4C_FALLBACK_MFT_UNAVAILABLE`.
   - В IPC-событии `L4C_EVENT_READY` (или текущих метриках) передаётся `video_encoder_backend = 1` (`OPENH264_SOFTWARE`), `video_hw_accel = 0`.
2. Зависший вызов в MFT обрабатывается watchdog супервизора: при превышении тайм-аута процесс корректно перезапускается с принудительным выбором программного кодера.

---

## 5. Интеграция в конвейер `l4capture` и CLI

1. **Логика выбора энкодера при старте конвейера (`src/main.c` / `pipeline.c`):**
   ```c
   /* 1. Попытка инициализации аппаратного Media Foundation MFT */
   if (l4c_mf_encoder_is_supported()) {
       status = l4c_mf_encoder_create(&ps->encoder);
       if (status == L4C_OK) {
           l4c_encoder_config_t enc_cfg = {
               .width = target_width,
               .height = target_height,
               .target_fps = target_fps,
               .target_bitrate_kbps = target_bitrate,
               .max_bitrate_kbps = max_bitrate,
               .input_format = L4C_PIX_FMT_NV12
           };
           status = ps->encoder->vtable->init(ps->encoder, &enc_cfg);
           if (status == L4C_OK) {
               ps->active_encoder_backend = 2; /* MF_HARDWARE */
           } else {
               ps->encoder->vtable->destroy(ps->encoder);
               ps->encoder = NULL;
           }
       }
   }

   /* 2. Гарантированный fallback на OpenH264 при неудаче или отсутствии MFT */
   if (!ps->encoder) {
       status = l4c_openh264_encoder_create(&ps->encoder);
       if (status != L4C_OK) return status;

       l4c_encoder_config_t enc_cfg = {
           .width = target_width,
           .height = target_height,
           .target_fps = target_fps,
           .target_bitrate_kbps = target_bitrate,
           .max_bitrate_kbps = max_bitrate,
           .input_format = L4C_PIX_FMT_I420
       };
       status = ps->encoder->vtable->init(ps->encoder, &enc_cfg);
       if (status != L4C_OK) return status;

       ps->active_encoder_backend = 1; /* OPENH264_SOFTWARE */
       if (l4c_mf_encoder_is_supported()) {
           ps->fallback_reason = L4C_FALLBACK_MFT_UNAVAILABLE;
       }
   }
   ```
2. **Маршрутизация цветовой конверсии:**
   - Если `ps->active_encoder_backend == 2`: вызов `l4c_color_convert_bgra_to_nv12()`.
   - Если `ps->active_encoder_backend == 1`: вызов `l4c_color_convert_bgra_to_i420()`.
3. **Передача телеметрии в IPC (§8):**
   - `video_encoder_backend`: `1=OPENH264_SOFTWARE`, `2=MF_HARDWARE`.
   - `video_hw_accel`: `1` (аппаратный MFT активен), `0` (программный OpenH264).
   - `video_fallback_reason`: `MFT_UNAVAILABLE` при переходе на OpenH264.
4. **Обновление сборочных файлов `build.cmd` и `test.cmd`:**
   - Добавить компиляцию `src\encoder\mf_encoder.c` для x86 и x64.
   - Проверить через `dumpbin /imports` отсутствие жестких статических библиотек Media Foundation.

---

## 6. Модульные и интеграционные тесты (`tests/test_mf_encoder.c`)

Создай тестовый модуль `tests/test_mf_encoder.c` и зарегистрируй его в `tests/test_runner.c`:

1. `test_mf_probe_graceful`:
   - Вызов `l4c_mf_encoder_is_supported()`.
   - Замер времени выполнения: гарантированно $\le 2.0$ секунд.
   - Проверка: отсутствие утечек COM-дескрипторов, деактивация всех объектов, корректный возврат `bool` без сбоев памяти.
2. `test_mf_create_destroy`:
   - Создание экземпляра через `l4c_mf_encoder_create`.
   - Проверка указателей виртуальной таблицы (`init`, `encode`, `force_idr`, `release_au`, `destroy`).
   - Уничтожение через `destroy()`, проверка отсутствия утечек.
3. `test_mf_init_types_and_sdp_compat`:
   - Инициализация с тестовой конфигурацией $854 \times 480$, 10 FPS, 500 кбит/с, формат `NV12`.
   - Проверка согласования входного и выходного медиа-типов (H.264 Baseline, Level 3.1).
4. `test_mf_color_convert_bgra_to_nv12`:
   - Модульный тест конвертера BGRA $\to$ NV12 на тестовых шаблонах (SMPTE полосы, чёрный, белый).
   - Проверка соответствия формулам ITU-R BT.601 limited range ($Y \in [16, 235]$, $UV \in [16, 240]$).
   - Проверка чередования $U$ и $V$ в плоскости UV и обработки страйдов.
5. `test_mf_nal_normalization_and_stripping`:
   - Подача битового потока с префиксами Annex B (`00 00 00 01`).
   - Проверка: все стартовые коды удалены, дескрипторы `l4c_nal_desc_t` указывают на заголовки NAL, длины соответствуют полезной нагрузке.
6. `test_mf_first_frame_idr_sps_pps`:
   - Проверка первого закодированного Access Unit:
   - Обязательное наличие NAL типа 7 (SPS), типа 8 (PPS) и типа 5 (IDR), флаг `is_idr = true`.
7. `test_mf_idr_cadence_and_sps_repetition`:
   - Серия из 30 кадров (эквивалент 3 секунд при 10 FPS).
   - Проверка: генерация IDR происходит не реже 1 раза в 2.0 секунды.
   - Проверка: перед каждым IDR-кадром обязательно повторяются актуальные SPS и PPS.
8. `test_mf_force_idr_coalescing`:
   - Несколько вызовов `force_idr()` с интервалами 100 мс, 200 мс и 600 мс.
   - Проверка: повторные запросы чаще 500 мс объединяются; запрос через 600 мс успешно выставляет флаг генерации ключевого кадра.
9. `test_mf_mft_failure_openh264_fallback`:
   - Имитация сбоя аппаратного MFT (недоступность оборудования или сбой `ProcessInput`).
   - Проверка: конвейер штатно переключается на `openh264_encoder` с форматом I420 без падения процесса и утечек дескрипторов.
10. `test_mf_stress_100_frames_zero_leak`:
    - Прогон 100 циклов кодирования кадров.
    - Проверка: отсутствие монотонного роста `Private Bytes` и дескрипторов процесса.

---

## 7. Порядок выполнения задачи агентом (Workflow)

### Шаг 1. Contract Gate
1. Открой `docs/l4capture/prompts/contract-handoff.md`.
2. Проверь наличие принятого блока `H-L4C-07-v1` со статусом `ACCEPTED` и валидными SHA-256 хэшами артефактов.
3. Проверь рабочую ветку Git: `l4capture/l4c-08-mf-encoder`.

### Шаг 2. Разработка и реализация
1. Создай заголовочный файл `include/l4capture/mf_encoder.h`.
2. Добавь функцию конверсии `l4c_color_convert_bgra_to_nv12` в `include/l4capture/color_convert.h` и `src/pipeline/color_convert.c`.
3. Реализуй модуль `src/encoder/mf_encoder.c` с динамической загрузкой `mfplat.dll`/`mf.dll`, зондированием hardware MFT, настройкой медиа-типов, нормализацией NAL и IDR-контролем.
4. Интегрируй автовыбор MFT/OpenH264 и маршрутизацию форматов NV12/I420 в `src/main.c`.
5. Реализуй тесты в `tests/test_mf_encoder.c` и подключи их к `tests/test_runner.c`.
6. Обнови сборочные скрипты `build.cmd` и `test.cmd`.

### Шаг 3. Локальная компиляция и верификация
1. Выполни сборку через `build.cmd all` (x86 и x64).
2. Запусти тесты через `test.cmd` (все тесты обязаны пройти со статусом PASS, 0 failures).
3. Проверь таблицу импортов через `dumpbin /imports bin\x86\l4capture.exe` и `bin\x64\l4capture.exe`: убедись в отсутствии статических версионных привязок к Media Foundation, блокирующих запуск на Windows 7.
4. Проведи замеры потребления CPU на доступном оборудовании (Intel/NVIDIA/AMD) и зафиксируй реальные показатели снижения нагрузки.

### Шаг 4. Оформление отчёта и Candidate (DETACHED_V1)
1. Создай отчёт `docs/l4capture/handoffs/L4C-08-MF-ENCODER-report.md`.
2. Вычисли SHA-256 реальных байтов отчёта.
3. Создай `docs/l4capture/handoffs/L4C-08-MF-ENCODER-candidate.md` со всеми полями канонического формата YAML.

---

## 8. Формат отчёта и Candidate (DETACHED_V1)

### Отчёт (`docs/l4capture/handoffs/L4C-08-MF-ENCODER-report.md`):
- Заголовок с указанием Prompt ID: `L4C-08-MF-ENCODER`.
- Перечень созданных и изменённых файлов строго внутри `tools/l4capture/`.
- Результаты компиляции MSVC `/W4 /WX /MT` для архитектур x86 и x64.
- Результаты выполнения модульных тестов (`test_runner`).
- Доказательства корректности работы probe $\le 2.0$ с и безопасного OpenH264 fallback.
- Проверка таблицы импортов (`dumpbin /imports`) с подтверждением отсутствия нежелательных runtime DLL.
- Реальные показатели потребления CPU при кодировании (hardware MFT vs OpenH264).

### Кандидат (`docs/l4capture/handoffs/L4C-08-MF-ENCODER-candidate.md`):
```yaml
handoff_id: H-L4C-08-v1
status: ACCEPTED
contract_kinds:
  - MF_HARDWARE_ENCODER
  - DYNAMIC_MF_LOADER
  - NV12_COLOR_CONVERTER
  - OPENH264_FALLBACK_CONTROLLER
producer_prompt_id: L4C-08-MF-ENCODER
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-08-MF-ENCODER-report.md
producer_branch: l4capture/l4c-08-mf-encoder
producer_commit: <git_commit_sha>
accepted_at_utc: <timestamp_iso8601>
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-08-MF-ENCODER-report.md
  - tools/l4capture/include/l4capture/mf_encoder.h
  - tools/l4capture/src/encoder/mf_encoder.c
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - <sha256_of_report_file>
  - <sha256_of_mf_encoder_h>
  - <sha256_of_mf_encoder_c>
  - <sha256_of_x86_exe>
  - <sha256_of_x64_exe>
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
    - H-L4C-06-v1
    - H-L4C-07-v1
  breaking_changes: false
  notes: Hardware H.264 MFT encoder implemented with dynamic Media Foundation loading from System32, NV12 color conversion, Annex B/AVCC stripping, IDR cadence <= 2.0s, force-IDR coalescing, and transparent fallback to OpenH264 on hardware absence or failure.
deployment_status: LOCAL_TESTS_PASSED
deployed_environment: local_build
feature_flags:
  l4capture_mf_encoder: enabled
  l4capture_openh264_fallback: enabled
contract_payload:
  encoder_backend:
    primary: MF_HARDWARE_H264
    fallback: OPENH264_SOFTWARE
    supported_os: Windows 8, 8.1, 10, 11 (Windows 7 falls back to OpenH264)
    profile_level: Constrained_Baseline_3_1
    sdp_compatibility: 42e01f
    input_format: NV12 (BT.601 limited)
    b_frames: 0 (disabled)
    latency_mode: zero_latency_low_delay
    probe_timeout_ms: <= 2000
    idr_interval_ms: <= 2000
    force_idr_coalesce_ms: 500
  color_conversion:
    nv12_matrix: BT.601 limited range
    subsampling: 2x2 chroma averaging
  fallback_policy:
    trigger: unsupported_os_or_hardware_probe_failure_or_runtime_error
    action: transparent_switch_to_openh264_with_i420
    status_reported: L4C_FALLBACK_MFT_UNAVAILABLE
supersedes: []
known_risks:
  - R3: MFT driver hang during probe — bounded by 2.0s timeout with supervisor watchdog.
  - R4: Windows 7 Media Foundation absence — eliminated via dynamic LoadLibraryExW from System32 and graceful OpenH264 fallback.
consumers:
  - L4C-09-PROFILES-DEGRADE
  - L4C-10-WIN7-TELEMETRY
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
next_prompt_id: L4C-09-PROFILES-DEGRADE
```
