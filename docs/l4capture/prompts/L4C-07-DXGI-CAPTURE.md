# L4C-07-DXGI-CAPTURE — Аппаратный захват экрана DXGI Desktop Duplication, staging/readback, обработка rotation/курсора и безопасный GDI fallback

```yaml
prompt_id: L4C-07-DXGI-CAPTURE
scope_project: tools/l4capture
scope_root: D:\repo\platerra\Public\etranprocessing\tools\l4capture
prompt_type: implementation-step
required_handoff_ids:
  - H-L4C-06-v1
sequence_gate_status: READY_FOR_L4C_07
output_handoff_id: H-L4C-07-v1
next_prompt_id: L4C-08-MF-ENCODER
branch: l4capture/l4c-07-dxgi-capture
report_path: docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-report.md
candidate_format: DETACHED_V1
candidate_path: docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-candidate.md
architecture_sections: [1, 2, 3, 4, 5, 7, 8, 9, 12, 13, 14]
consumers:
  - L4C-08-MF-ENCODER
  - L4C-09-PROFILES-DEGRADE
  - L4C-10-WIN7-TELEMETRY
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
```

---

## 1. Цель и архитектурная миссия

Ты выступаешь в роли **Ведущего системного инженера native-графики Windows и разработчика аппаратного видеозахвата (L4Capture-DXGICapture-Agent)** в рамках комплекса `tools suite`.

Твоя задача — реализовать высокопроизводительный нативный модуль аппаратного видеозахвата экрана на базе технологии **DirectX Graphics Infrastructure (DXGI 1.2 Desktop Duplication API)** и **Direct3D 11** в целевой директории `tools\l4capture\`.

### Архитектурный контекст и ценность
На шагах Фазы 1 (L4C-01 .. L4C-06) был успешно создан, изолирован и сквозным образом верифицирован базовый тракт захвата на базе GDI и программного кодера OpenH264 (Milestone M-1 принят владельцем с вердиктом «НОРМ»). Базовый GDI-тракт гарантирует 100% совместимость со всеми версиями Windows (включая Windows 7 SP1 x86/x64), но на современных операционных системах (Windows 8, 8.1, 10, 11) создаёт повышенную нагрузку на CPU из-за программного копирования видеобуфера через GDI BitBlt/DIBSection.

Фаза 2 открывается шагом **L4C-07-DXGI-CAPTURE**, призванным максимизировать общую системную эффективность и минимизировать непроизводительные расходы вычислительных ресурсов платёжных терминалов за счёт прямого аппаратного доступа к буферам DWM (Desktop Window Manager) через GPU:
1. **DXGI Desktop Duplication Backend (`dxgi_capture.c` / `include/l4capture/dxgi_capture.h`):** Реализация интерфейса `l4c_capture_backend_vtable_t` на базе интерфейсов `IDXGIOutputDuplication`, `IDXGIOutput1`, `ID3D11Device`, `ID3D11DeviceContext`.
2. **Динамическая загрузка D3D11/DXGI (LoadLibrary из System32):** Строгий запрет жесткой статической привязки к `d3d11.dll` и `dxgi.dll` для сохранения абсолютной запускаемости и работоспособности бинарников `l4capture.exe` на Windows 7 SP1 без внешних зависимостей.
3. **Staging Texture & CPU Readback:** Эффективный перенос видеокадра из GPU VRAM в системную память через выделенную staging-текстуру (`D3D11_USAGE_STAGING`, `D3D11_CPU_ACCESS_READ`) с `RowPitch`-выравниванием и поддержкой формата `DXGI_FORMAT_B8G8R8A8_UNORM`.
4. **Обработка поворота монитора (`DXGI_MODE_ROTATION`):** Учёт ориентации экрана (Identity, Rotate90, Rotate180, Rotate270) с корректной нормализацией геометрии до этапа масштабирования.
5. **Аппаратный курсор в DXGI:** Извлечение формы и положения курсора через `DXGI_OUTDUPL_FRAME_INFO` и `GetFramePointerShape`, определение признака уже включённого курсора и наложение формы ровно один раз без дублирования.
6. **Принцип Fail-Closed и безопасный fallback на GDI:**
   - При `DXGI_ERROR_ACCESS_LOST`: сначала мгновенная проверка доступности интерактивной пользовательской сессии через `safety_gate` (`OpenInputDesktop`).
   - Если сессия недоступна (UAC, экран блокировки Win+L, смена сессии, RDP disconnect): немедленная остановка захвата и прекращение RTP-потока $\le 500$ мс (fail-closed, запрет показа старых кадров и черных заглушек).
   - Если сессия доступна: выполнение до 3 попыток реинициализации DXGI (паузы 100/300/1000 мс). Если реинициализация не удалась — автоматический прозрачный переход на проверенный GDI-бэкенд с фиксацией причины `L4C_FALLBACK_DXGI_ACCESS_LOST`.
   - При запросе виртуального многомониторного десктопа: прозрачный запуск GDI (DXGI в v1 строго привязан к одному физическому output).
7. **Интеграция в конвейер и диагностику:** Автоматический выбор бэкенда при старте, передача типа активного бэкенда (`capture_backend = 2` для DXGI, `1` для GDI) в событии `L4C_EVENT_READY` и отчётах метрик.
8. **Автономный тестовый набор (`tests/test_dxgi_capture.c`):** Тесты probe, жизненного цикла, staging, обработки таймаутов, отсутствия утечек видеопамяти, ротации, курсора и сценариев деградации/fallback.

---

## 2. Непереговорные рамочные принципы и изоляция

1. **Строгая изоляция директорий (Directory Boundary):**
   - **Код, заголовки, сборка и тесты:** размещаются строго в `tools\l4capture\`.
   - **Документация, промпты, журнал контрактов:** находятся в `docs\l4capture\`.
   - **Отчёты и candidate-файлы:** формируются строго в `docs\l4capture\handoffs\`.
   - **КАТЕГОРИЧЕСКИЙ ЗАПРЕТ `l4desk-service`:** Строжайше запрещено создавать, изменять или использовать файлы внутри каталога `l4desk-service`. Проект `l4capture` полностью автономен.
   - **ЗАПРЕТ на модификацию других подсистем:** Запрещено изменять файлы в `BACK\`, `FRONT\`, `tools\l4desk`, `ProcessingBackend\`, `MenuBuilder\`, `shared\`, `sqlFileExample\`, `stored-procedures\`.
2. **Стандарты чистого C и компиляции:**
   - Код пишется строго на **C99/C11** (подмножество MSVC, компиляция через `cl.exe`, не C++). Доступ к COM/DXGI/D3D11 осуществляется исключительно через C-интерфейсы (`lpVtbl`).
   - Статическая компоновка Runtime: обязательный флаг `/MT` для Release-конфигурации (никаких динамических зависимостей `msvcrt*.dll`, `vcruntime*.dll`).
   - Поддержка Windows 7 SP1 x86/x64: компиляция с `/D_WIN32_WINNT=0x0601`, компоновка x86 с `/SUBSYSTEM:CONSOLE,6.01`, x64 с `/SUBSYSTEM:CONSOLE`.
   - Нулевая толерантность к предупреждениям компилятора: уровень `/W4` с zero warnings.
3. **Безопасная загрузка системных API (System Directory Only):**
   - Согласно §3 архитектуры, любые опциональные библиотеки современных ОС (`d3d11.dll`, `dxgi.dll`) должны загружаться **строго из системного каталога** (`LoadLibraryExW(..., NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)`).
   - Запрещена статическая линковка, приводящая к отказу старта `l4capture.exe` на Windows 7 из-за отсутствия экспортов DXGI 1.2. Проверка через `dumpbin /imports` обязательна.
4. **Ресурсная дисциплина и Zero-Leakage Invariant:**
   - **Безусловный `ReleaseFrame`:** Каждый успешно полученный кадр через `AcquireNextFrame` **ОБЯЗАН** освобождаться через `IDXGIOutputDuplication::ReleaseFrame()`, включая любые ветки ошибок, таймаутов маппинга или аварийного прерывания сессии.
   - **Zero-Allocation на кадр:** Staging-текстура создаётся однократно в `init`. На каждом кадре запрещено создавать/перевыделять текстуры D3D11.
   - Обязательное освобождение всех промежуточных COM-объектов (`Release()`) при ошибках и в `destroy()`.
5. **Принцип безопасности Fail-Closed и тайминги:**
   - Ошибка `DXGI_ERROR_ACCESS_LOST` или невозможность захвата десктопа в заблокированном состоянии не должна скрываться генерацией искусственных/старых кадров.
   - Остановка потока при недоступности сессии $\le 500$ мс без drain и без автоперезапуска после снятия блокировки.

---

## 3. Pre-Flight Check & Contract Gate (Шаг 1)

Перед началом внесения изменений агент обязан выполнить валидацию входных контрактов:

1. Открой файл журнала `docs\l4capture\prompts\contract-handoff.md`.
2. Убедись, что блок `H-L4C-06-v1` присутствует в секции `## 5. Принятые handoff-блоки` со статусом `ACCEPTED`.
3. Сверь контрольную сумму входного артефакта вехи M-1:
   - `docs/l4capture/handoffs/L4C-06-MILESTONE-LIVE-VERIFY-report.md`: `425d1e38556f6d3a0127bcedc3c7f85a62351104d9dc49b0b045a3160511beda`
4. Проверь наличие подтверждённого вердикта владельца `owner_verdict: NORM` в блоке `H-L4C-06-v1`.
5. Проверь рабочую ветку Git: `l4capture/l4c-07-dxgi-capture`.
6. При обнаружении несоответствий или повреждений заверши работу со статусом `BLOCKED_CONTRACT`.

---

## 4. Архитектурные требования и техническая спецификация

Реализуй бэкенд DXGI Desktop Duplication в строгом соответствии с C-интерфейсом `l4c_capture_backend_t` (`include/l4capture/capture_backend.h`):

```c
typedef struct l4c_capture_backend_vtable {
    l4c_status_t (*init)(struct l4c_capture_backend *self, const l4c_capture_config_t *config);
    l4c_status_t (*acquire_frame)(struct l4c_capture_backend *self, l4c_frame_view_t *out_frame, uint32_t timeout_ms);
    void (*release_frame)(struct l4c_capture_backend *self, l4c_frame_view_t *frame);
    void (*destroy)(struct l4c_capture_backend *self);
} l4c_capture_backend_vtable_t;
```

### 4.1. Заголовочный интерфейс DXGI Capture (`include/l4capture/dxgi_capture.h`)

Создай заголовочный файл `include/l4capture/dxgi_capture.h`:

```c
#ifndef L4C_DXGI_CAPTURE_H
#define L4C_DXGI_CAPTURE_H

#include "capture_backend.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * Проверка поддержки DXGI Desktop Duplication на текущей ОС/оборудовании.
 * Выполняет безопасный probe без утечек памяти за время <= 2 секунд.
 * Возвращает true на Windows 8+ с поддержкой DXGI 1.2 и D3D11;
 * Возвращает false на Windows 7 или при отсутствии совместимого оборудования.
 */
bool l4c_dxgi_capture_is_supported(void);

/* 
 * Фабричный метод создания экземпляра DXGI capture backend.
 * Выделяет структуру бэкенда и привязывает виртуальную таблицу функций.
 */
l4c_status_t l4c_dxgi_capture_create(l4c_capture_backend_t **out_backend);

#ifdef __cplusplus
}
#endif

#endif /* L4C_DXGI_CAPTURE_H */
```

### 4.2. Безопасная динамическая инициализация D3D11 / DXGI (Win7 Compatibility)

В файле `src/capture/dxgi_capture.c` реализуй механизм безопасного динамического связывания:
1. Загрузка `d3d11.dll` и `dxgi.dll` через `LoadLibraryExW(L"d3d11.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)`.
2. Извлечение адреса функции `D3D11CreateDevice`:
   ```c
   typedef HRESULT (WINAPI *PFN_D3D11_CREATE_DEVICE)(
       IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
       const D3D_FEATURE_LEVEL*, UINT, UINT,
       ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**
   );
   ```
3. Извлечение функции `CreateDXGIFactory1`:
   ```c
   typedef HRESULT (WINAPI *PFN_CREATE_DXGI_FACTORY1)(REFIID, void**);
   ```
4. Если библиотеки отсутствуют или функции не найдены (типично для Windows 7 SP1 без Platform Update): функция `l4c_dxgi_capture_is_supported()` возвращает `false`, а фабрика корректно возвращает отказ без падения процесса.

### 4.3. Выбор Output и ограничение виртуального десктопа (§4, §5.1)

1. **Одиночный Output:**
   - Модуль ищет `IDXGIOutput`, соответствующий запрошенным координатам `config->target_rect`.
   - Получает интерфейс `IDXGIOutput1` через `QueryInterface(..., &IID_IDXGIOutput1, ...)`.
   - Инициализирует дублирование через вызов `IDXGIOutput1::DuplicateOutput(pD3DDevice, &pOutputDuplication)`.
2. **Ограничение многомониторного виртуального десктопа:**
   - Согласно §4 и §5.1 архитектуры: виртуальный десктоп, объединяющий несколько мониторов, не композитится в GPU в рамках v1.
   - Если запрошенный `target_rect` превышает границы одиночного физического монитора либо представляет собой общий виртуальный десктоп из нескольких экранов, `dxgi_capture_init` возвращает `L4C_ERR_INVALID_ARG` (что запускает штатный fallback на GDI, захватывающий весь виртуальный экран).

### 4.4. Staging Texture, CPU Readback и Zero-Allocation (§5.1, §13.2)

1. **Инициализация Staging Texture (`init`):**
   - Получение дескриптора текущего режима `DXGI_OUTDUPL_DESC`.
   - Создание staging-текстуры `ID3D11Texture2D` в оперативной памяти драйвера:
     ```c
     D3D11_TEXTURE2D_DESC desc;
     memset(&desc, 0, sizeof(desc));
     desc.Width = dupl_desc.ModeDesc.Width;
     desc.Height = dupl_desc.ModeDesc.Height;
     desc.MipLevels = 1;
     desc.ArraySize = 1;
     desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
     desc.SampleDesc.Count = 1;
     desc.Usage = D3D11_USAGE_STAGING;
     desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
     desc.BindFlags = 0;
     desc.MiscFlags = 0;
     pDevice->lpVtbl->CreateTexture2D(pDevice, &desc, NULL, &pStagingTexture);
     ```
   - Запрещено перевыделять staging-текстуру на каждом кадре.
2. **Захват кадра (`acquire_frame`):**
   - Вызов `pDuplication->lpVtbl->AcquireNextFrame(pDuplication, timeout_ms, &frame_info, &pResource)`.
   - При `DXGI_ERROR_WAIT_TIMEOUT`: кадр на рабочем столе не изменился. Возврат `L4C_ERR_NO_FRAME` (нормальное состояние, освобождение кадра не требуется).
   - При ошибке `DXGI_ERROR_ACCESS_LOST`: переход к процедуре обработки потери доступа (§4.7).
   - Получение интерфейса `ID3D11Texture2D` из `pResource`.
   - Копирование видеоданных из VRAM в staging-текстуру:
     `pContext->lpVtbl->CopyResource(pContext, (ID3D11Resource*)pStagingTexture, (ID3D11Resource*)pAcquiredTexture)`.
   - Маппинг памяти staging-текстуры:
     `pContext->lpVtbl->Map(pContext, (ID3D11Resource*)pStagingTexture, 0, D3D11_MAP_READ, 0, &mapped)`.
   - Заполнение структуры `l4c_frame_view_t`:
     - `data = (const uint8_t*)mapped.pData`;
     - `stride = (int32_t)mapped.RowPitch`;
     - `width = dupl_desc.ModeDesc.Width`;
     - `height = dupl_desc.ModeDesc.Height`;
     - `physical_rect = target_rect`;
     - `pts_ms = l4c_now_ms()`;
     - `geometry_generation = current_generation`.
3. **Освобождение кадра (`release_frame`):**
   - Анмаппинг: `pContext->lpVtbl->Unmap(pContext, (ID3D11Resource*)pStagingTexture, 0)`.
   - Освобождение ресурса кадра: `pResource->lpVtbl->Release(pResource)`.
   - **Обязательный вызов:** `pDuplication->lpVtbl->ReleaseFrame(pDuplication)`.
   - **Инвариант:** Кадр освобождается всегда, даже если последующие этапы конвейера завершились с ошибкой. Неосвобождённый кадр приводит к зависанию DXGI-дубликатора на следующем тике.

### 4.5. Обработка поворота монитора (`DXGI_MODE_ROTATION`)

Согласно §5.1, ориентация вывода должна учитываться до передачи в модуль масштабирования:
- Значения `dupl_desc.Rotation`:
  - `DXGI_MODE_ROTATION_IDENTITY`: нормальная ориентация.
  - `DXGI_MODE_ROTATION_ROTATE90`: экран повёрнут на 90 градусов. Ширина и высота меняются местами.
  - `DXGI_MODE_ROTATION_ROTATE180`: экран повёрнут на 180 градусов.
  - `DXGI_MODE_ROTATION_ROTATE270`: экран повёрнут на 270 градусов. Ширина и высота меняются местами.
- При наличии поворота: бэкенд выполняет коррекцию координат или предварительную перестановку пикселей/шагов строк во временный буфер кадра для гарантии получения правильной геометрии перед масштабированием в `scale.c`.

### 4.6. Аппаратный курсор в DXGI (§5.2)

1. Анализ информации курсора в `DXGI_OUTDUPL_FRAME_INFO`:
   - `frame_info.LastMouseUpdateTime`: временная метка последнего обновления курсора.
   - `frame_info.PointerPosition.Visible`: флаг видимости курсора.
   - `frame_info.PointerPosition.Position`: физические экранные координаты курсора.
2. Обновление формы указателя:
   - Если `frame_info.PointerShapeBufferSize > 0`: буфер формы курсора изменился.
   - Выделение буфера формы и вызов `pDuplication->lpVtbl->GetFramePointerShape(..., &shape_info)`.
   - Поддерживаемые типы форм (`shape_info.Type`):
     - `DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME`
     - `DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR`
     - `DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR`
3. Кэширование и наложение:
   - Кэширование текущей формы курсора и hotspot в контексте бэкенда.
   - Наложение формы курсора поверх данных кадра в staging-буфере только если курсор видим и находится в пределах видимой области.
   - **Инвариант §5.2:** Рисовать указатель ровно один раз. Если драйвер уже внедрил курсор в изображение поверхности, повторная отрисовка запрещена.

### 4.7. Обработка ошибок, UAC, ACCESS_LOST и безопасный Fallback (§5.1, §9.3, §14)

При возврате `DXGI_ERROR_ACCESS_LOST` реализуется строгий двухступенчатый алгоритм:
1. **Проверка доступности интерактивной сессии:**
   - Выполняется проверка десктопа через вызов `l4c_safety_gate_check()` или прямой опрос `OpenInputDesktop()`.
   - **Если сессия недоступна (UAC, экран приветствия/Win+L, RDP disconnect, смена сессии):**
     - Состояние: `L4C_ERR_SESSION_UNAVAILABLE`.
     - Мгновенная остановка: RTP-поток прекращается в течение $\le 500$ мс.
     - Очистка всех очередей, drain запрещён.
     - Запрещено пытаться переподключаться к экрану ввода или переходить на GDI (GDI также вернёт чёрный экран или отказ доступа).
     - Запрещён автоперезапуск после снятия блокировки без новой авторизованной команды `stream_start`.
2. **Обработка при доступной сессии (смена режима видеоадаптера / разрешения):**
   - Если сессия доступна, выполняется до трёх попыток реинициализации DXGI:
     - Попытка 1: пауза 100 мс $\to$ reinit DXGI.
     - Попытка 2: пауза 300 мс $\to$ reinit DXGI.
     - Попытка 3: пауза 1000 мс $\to$ reinit DXGI.
   - Любое внешнее событие `CMD_STOP` или истечение deadline аренды немедленно прерывает ожидание.
   - Если за 3 попытки восстановить DXGI не удалось:
     - Выполняется контролируемый **fallback на GDI Capture Backend** (`l4c_gdi_capture_create`).
     - Сохраняется исходная конфигурация захвата `target_rect`.
     - Фиксируется диагностическая причина: `L4C_FALLBACK_DXGI_ACCESS_LOST`.
     - Конвейер продолжает работу без падения процесса.

---

## 5. Интеграция в конвейер `l4capture` и CLI

1. **Фабрика выбора бэкенда (`src/capture/capture_factory.c` или обновление `src/main.c`):**
   - При старте конвейера (`pipeline_start`):
     ```c
     if (l4c_dxgi_capture_is_supported() && is_single_output(&cap_cfg.target_rect)) {
         status = l4c_dxgi_capture_create(&ps->capture);
         if (status == L4C_OK) {
             status = ps->capture->vtable->init(ps->capture, &cap_cfg);
             if (status == L4C_OK) {
                 ps->active_capture_backend = 2; /* DXGI */
             } else {
                 ps->capture->vtable->destroy(ps->capture);
                 ps->capture = NULL;
             }
         }
     }
     /* Fallback на GDI при неудаче или отсутствии поддержки */
     if (!ps->capture) {
         status = l4c_gdi_capture_create(&ps->capture);
         if (status != L4C_OK) return status;
         status = ps->capture->vtable->init(ps->capture, &cap_cfg);
         if (status != L4C_OK) return status;
         ps->active_capture_backend = 1; /* GDI */
         if (l4c_dxgi_capture_is_supported()) {
             ps->fallback_reason = L4C_FALLBACK_DXGI_ACCESS_LOST;
         }
     }
     ```
2. **Передача статуса в IPC-событиях (§8):**
   - В сообщении `L4C_EVENT_READY` поле `capture_backend` устанавливается в:
     - `1` — GDI
     - `2` — DXGI
   - В сообщении `L4C_EVENT_METRICS` передаются актуальные показатели и причина деградации/fallback.
3. **Обновление скрипта сборки `build.cmd`:**
   - Добавить компиляцию `src\capture\dxgi_capture.c` для x86 и x64.
   - Убедиться, что не добавлены жесткие зависимости от `d3d11.lib` / `dxgi.lib` в команду `link.exe`, либо они безопасно резолвятся.
   - Проверить через `dumpbin /imports` чистоту таблицы импортов на отсутствие версионных привязок к DXGI 1.2 на Windows 7.

---

## 6. Модульные и интеграционные тесты (`tests/test_dxgi_capture.c`)

Создай тестовый модуль `tests/test_dxgi_capture.c` и подключи его к автономному тест-раннеру `test_runner.c`:

1. `test_dxgi_probe_graceful`:
   - Вызов `l4c_dxgi_capture_is_supported()`.
   - Проверка: на Windows 10/11 возвращает `true`, на тестовых заглушках или Windows 7 — `false`, без сбоев памяти и крашей.
2. `test_dxgi_create_destroy`:
   - Создание экземпляра через `l4c_dxgi_capture_create`.
   - Валидация указателей виртуальной таблицы (`init`, `acquire_frame`, `release_frame`, `destroy`).
   - Уничтожение бэкенда через `destroy()`.
3. `test_dxgi_init_and_single_output_validation`:
   - Инициализация с валидными координатами одиночного монитора.
   - Проверка возврата `L4C_OK` при поддержке DXGI.
4. `test_dxgi_virtual_desktop_rejection`:
   - Инициализация с прямоугольником, заведомо охватывающим несколько дисплеев или отрицательный origin вне основного экрана.
   - Проверка возврата ошибки/сигнала о необходимости GDI-захвата.
5. `test_dxgi_acquire_timeout_no_frame`:
   - Вызов `acquire_frame` с таймаутом 10 мс в статичном окружении.
   - Проверка возврата `L4C_ERR_NO_FRAME`. Проверка отсутствия необходимости вызова `release_frame`.
6. `test_dxgi_release_frame_leak_stress`:
   - Стресс-тест: 100 циклов `acquire_frame` и `release_frame`.
   - Проверка стабильности handle count и отсутствия утечек видеопамяти / D3D11 staging textures.
7. `test_dxgi_rotation_transform`:
   - Проверка логики трансформации размеров для режимов `DXGI_MODE_ROTATION_ROTATE90`, `ROTATE180`, `ROTATE270`.
8. `test_dxgi_cursor_shape_handling`:
   - Проверка обработки монохромных и цветных масок курсора DXGI.
9. `test_dxgi_access_lost_session_dead`:
   - Имитация получения `DXGI_ERROR_ACCESS_LOST` при заблокированном рабочем столе.
   - Проверка мгновенного возврата `L4C_ERR_SESSION_UNAVAILABLE` без попыток reinit и без задержек.
10. `test_dxgi_access_lost_retry_and_gdi_fallback`:
    - Имитация `DXGI_ERROR_ACCESS_LOST` при активной пользовательской сессии.
    - Проверка выполнения 3 попыток reinit и последующего плавного перехода на GDI бэкенд с сохранением кадрового потока.

---

## 7. Порядок выполнения задачи агентом (Workflow)

### Шаг 1. Contract Gate
1. Открой `docs/l4capture/prompts/contract-handoff.md`.
2. Проверь наличие принятого блока `H-L4C-06-v1` со статусом `ACCEPTED` и хешем `425d1e38556f6d3a0127bcedc3c7f85a62351104d9dc49b0b045a3160511beda`.
3. Зафиксируй прохождение вехи M-1.

### Шаг 2. Разработка и реализация
1. Создай заголовок `include/l4capture/dxgi_capture.h`.
2. Реализуй модуль захвата `src/capture/dxgi_capture.c` с динамической загрузкой D3D11/DXGI.
3. Интегрируй fallback на GDI при ошибках доступа или неподдерживаемых режимах.
4. Обнови `src/main.c` для поддержки автовыбора DXGI/GDI.
5. Реализуй тесты в `tests/test_dxgi_capture.c` и зарегистрируй их в `tests/test_runner.c`.
6. Обнови сборочные скрипты `build.cmd` и `test.cmd`.

### Шаг 3. Локальная компиляция и верификация
1. Выполни компиляцию через `build.cmd all` (x86 и x64).
2. Запусти тесты через `test.cmd` (все тесты обязаны завершиться со статусом PASS, 0 failures).
3. Проверь таблицу импортов через `dumpbin /imports bin\x86\l4capture.exe` и `bin\x64\l4capture.exe`. Убедись, что отсутствуют жесткие статические зависимости, блокирующие Win7.
4. Проведи сравнительный замер потребления CPU между GDI и DXGI при захвате 1080p $\to$ 480p 10 FPS на локальном тестовом стенде. Зафиксируй реальные показатели.

### Шаг 4. Оформление отчёта и Candidate (DETACHED_V1)
1. Создай отчёт `docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-report.md`.
2. Вычисли SHA-256 реальных байтов отчёта.
3. Создай `docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-candidate.md` со всеми полями канонического формата YAML.

---

## 8. Формат отчёта и Candidate (DETACHED_V1)

### Отчёт (`docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-report.md`):
- Заголовок с указанием Prompt ID: `L4C-07-DXGI-CAPTURE`.
- Список созданных и модифицированных файлов строго внутри `tools/l4capture/`.
- Результаты компиляции x86/x64 без предупреждений (`/W4`).
- Результаты выполнения модульных тестов (`test_runner`).
- Доказательства корректной обработки `DXGI_ERROR_ACCESS_LOST` и переключения на GDI fallback.
- Таблица импортов (`dumpbin /imports`) с подтверждением Win7-совместимости.
- Сравнительные метрики CPU/памяти (GDI vs DXGI) без необоснованных утверждений об ускорении «в 2–3 раза».

### Кандидат (`docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-candidate.md`):
```yaml
handoff_id: H-L4C-07-v1
status: ACCEPTED
contract_kinds:
  - DXGI_CAPTURE_BACKEND
  - DYNAMIC_D3D11_LOADER
  - GDI_FALLBACK_CONTROLLER
  - ROTATION_CURSOR_HANDLER
producer_prompt_id: L4C-07-DXGI-CAPTURE
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-report.md
producer_branch: l4capture/l4c-07-dxgi-capture
producer_commit: <git_commit_sha>
accepted_at_utc: <timestamp_iso8601>
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-report.md
  - tools/l4capture/include/l4capture/dxgi_capture.h
  - tools/l4capture/src/capture/dxgi_capture.c
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - <sha256_of_report_file>
  - <sha256_of_dxgi_capture_h>
  - <sha256_of_dxgi_capture_c>
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
  breaking_changes: false
  notes: DXGI 1.2 Desktop Duplication backend implemented with dynamic D3D11 loading, zero-copy staging texture readback, hardware cursor composition, screen rotation support, and graceful automatic GDI fallback on ACCESS_LOST.
deployment_status: LOCAL_TESTS_PASSED
deployed_environment: local_build
feature_flags:
  l4capture_dxgi_capture: enabled
  l4capture_gdi_fallback: enabled
contract_payload:
  capture_backend:
    primary: DXGI_1_2_DESKTOP_DUPLICATION
    fallback: GDI_BITBLT
    supported_os: Windows 8, 8.1, 10, 11 (Windows 7 falls back to GDI)
    staging_format: DXGI_FORMAT_B8G8R8A8_UNORM
    rotation_handling: [IDENTITY, ROTATE90, ROTATE180, ROTATE270]
    cursor_overlay: hardware_shape_cache_single_render
  error_handling:
    access_lost_policy: fail_closed_if_session_unavailable_else_3_retries_then_gdi_fallback
    retry_delays_ms: [100, 300, 1000]
    stop_latency_ms: <= 500
    leak_prevention: unconditional_release_frame
supersedes: []
known_risks:
  - R1: UAC/Lock screen causes DXGI_ERROR_ACCESS_LOST — verified fail-closed <= 500ms without leak or hanging.
  - R3: Staging buffer leak — verified unconditional ReleaseFrame across 100 stress cycles.
  - R4: Windows 7 incompatibility — eliminated via dynamic LoadLibraryExW from System32.
consumers:
  - L4C-08-MF-ENCODER
  - L4C-09-PROFILES-DEGRADE
  - L4C-10-WIN7-TELEMETRY
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
next_prompt_id: L4C-08-MF-ENCODER
```

---

## 9. Критерии приёмки и Definition of Done

1. **Изоляция каталогов:** Изменения затронули строго файлы в директории `tools/l4capture/`. Каталог `l4desk-service` и сторонние сервисы не модифицировались.
2. **Чистый C и компиляция:** Сборка x86 и x64 в `build.cmd` проходит без ошибок и предупреждений компилятора (`/W4`, `/MT`).
3. **Win7-безопасность:** Отсутствуют жесткие статические импорты DXGI 1.2, препятствующие запуску на Windows 7. Динамическая загрузка выполняется строго из `System32`.
4. **Zero-Leakage Invariant:** Вызов `ReleaseFrame` гарантированно выполняется при каждом заборе кадра, включая ошибки и таймауты. Утечки D3D11-ресурсов отсутствуют.
5. **Безопасность сессии (Fail-Closed):** При `DXGI_ERROR_ACCESS_LOST` и заблокированном рабочем столе трансляция прекращается за время $\le 500$ мс без зависаний.
6. **Надёжность Fallback:** При доступной сессии и потере DXGI-устройства выполняется автоматическое переключение на GDI с сохранением непрерывности видеопотока.
7. **Тестовое покрытие:** Все тесты в `tests/test_dxgi_capture.c` успешно проходят в `test.cmd`.
8. **Артефакты:** Подготовлены отчёт `L4C-07-DXGI-CAPTURE-report.md` и кандидат `L4C-07-DXGI-CAPTURE-candidate.md` в формате `DETACHED_V1`.
