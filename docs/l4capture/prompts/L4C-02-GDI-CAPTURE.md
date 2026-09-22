# L4C-02-GDI-CAPTURE — GDI-захват экрана и курсора, билинейное масштабирование, цветовая конверсия I420 и pacing-конвейер

```yaml
prompt_id: L4C-02-GDI-CAPTURE
scope_project: tools/l4capture
scope_root: D:\repo\platerra\Public\etranprocessing\tools\l4capture
prompt_type: implementation-step
required_handoff_ids:
  - H-L4C-01-v1
sequence_gate_status: READY_FOR_L4C_02
output_handoff_id: H-L4C-02-v1
next_prompt_id: L4C-03-OPENH264-CODEC
branch: l4capture/l4c-02-gdi-capture
report_path: docs/l4capture/handoffs/L4C-02-GDI-CAPTURE-report.md
candidate_format: DETACHED_V1
candidate_path: docs/l4capture/handoffs/L4C-02-GDI-CAPTURE-candidate.md
architecture_sections: [1, 2, 3, 4, 5, 7, 8, 9, 12, 13, 14]
consumers:
  - L4C-03-OPENH264-CODEC
  - L4C-04-RTP-SENDER
  - L4C-05-AGENT-ADAPTER
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
```

---

## 1. Цель и архитектурная миссия

Ты выступаешь в роли **Ведущего системного инженера native-графики Windows и разработчика видеоконвейера (L4Capture-GDICapture-Agent)** в рамках комплекса `tools suite`.

Твоя задача — реализовать полнофункциональный и надёжный видеотракт первичного захвата экрана и подготовки видеокадров в целевой директории `tools\l4capture\`. Модуль GDI-захвата является фундаментальным базовым трактом (baseline fallback), гарантирующим работоспособность на любых редакциях Windows (начиная с Windows 7 SP1 x86/x64) без сторонних графических драйверов и GPU-ускорителей.

На шаге **L4C-02-GDI-CAPTURE** создаются ключевые компоненты конвейера формирования изображения согласно архитектурным требованиям `l4capture_arch_final.md`:
1. **GDI Capture Backend (`gdi_capture.c`):** Реализация интерфейса `ICaptureBackend` с захватом первичного монитора, виртуального десктопа или заданного прямоугольника физических координат (включая отрицательный origin), с обязательным переиспользованием ресурсов `CreateDIBSection`/DC (zero-allocation на кадр).
2. **Наложение аппаратного курсора (`cursor.c`):** Снятие формы и позиции указателя через `GetCursorInfo`, извлечение hotspot через `GetIconInfo`, отрисовка через `DrawIconEx` и **гарантированное освобождение дескрипторов GDI (`DeleteObject`)** для полного исключения утечек ресурсов (парирование риска R3).
3. **Билинейное масштабирование растра (`scale.c`):** Высокоскоростное математически точное масштабирование на чистом C (целочисленная арифметика фиксированной точки 16.16) из произвольного разрешения в профиль `base_480p` (`854x480`) без обрезки и полей для сохранения 1:1 соотношения нормализованных координат ввода.
4. **Цветовая конверсия BGRA $\to$ I420 (`color_convert.c`):** Преобразование 32-bit BGRA top-down в планарный I420 со строгим соблюдением стандарта **BT.601 limited range** (Studio Swing, Y: 16–235, U/V: 16–240), субдискретизацией хромы $2 \times 2$ и выравниванием страйдов плоскостей по 16 байт.
5. **Pacing-конвейер кадров (`pipeline.c`):** Монотонный тактовый генератор 10 FPS (100 мс) с семантикой `latest-frame` (ровно 1 кадр в обработке, не более 1 ожидающего со сбросом устаревших) и интеграцией с `safety_gate` (мгновенная остановка и сброс слотов при lock/UAC/дисконнекте).
6. **Автономный тестовый набор (`tests/`):** Комплекс модульных тестов, проверяющих точность цвета, границы интерполяции, работу с отрицательными координатами, устойчивость к деградации и стресс-тест отсутствия утечек GDI-хэндлов.

---

## 2. Непереговорные рамочные принципы и изоляция

1. **Строгая изоляция директорий (Directory Boundary):**
   - **Код, заголовки, сборка и тесты:** размещаются строго в `tools\l4capture\`.
   - **Документация, промпты, журнал контрактов:** находятся в `docs\l4capture\`.
   - **Отчёты и candidate-файлы:** формируются строго в `docs\l4capture\handoffs\`.
   - **КАТЕГОРИЧЕСКИЙ ЗАПРЕТ `l4desk-service`:** Строжайше запрещено создавать, изменять или использовать файлы внутри каталога `l4desk-service`. Проект `l4capture` полностью изолирован.
   - **ЗАПРЕТ на модификацию других подсистем:** Запрещено изменять файлы в `BACK\`, `FRONT\`, `tools\l4desk`, `ProcessingBackend\`, `MenuBuilder\`, `shared\`, `sqlFileExample\`, `stored-procedures\`.
2. **Стандарты чистого C и компиляции:**
   - Код пишется строго на **C99/C11** (подмножество MSVC, компиляция через `cl.exe`, не C++).
   - Статическая компоновка Runtime: обязательный флаг `/MT` для Release-конфигурации (никаких динамических зависимостей `msvcrt*.dll`, `vcruntime*.dll`).
   - Поддержка Windows 7 SP1 x86/x64: компиляция с `/D_WIN32_WINNT=0x0601`, компоновка x86 с `/SUBSYSTEM:CONSOLE,6.01`, x64 с `/SUBSYSTEM:CONSOLE`.
   - Таблица импортов (`dumpbin /dependents`): только базовые системные библиотеки (`KERNEL32.dll`, `USER32.dll`, `ADVAPI32.dll`, `GDI32.dll`, `WS2_32.dll`, `OLE32.dll`).
   - Нулевая толерантность к предупреждениям компилятора: уровень `/W4` с zero warnings.
3. **Ресурсная дисциплина (Zero-Leakage & Zero-Allocation):**
   - **Запрет аллокаций в горячем цикле:** Все рабочие буферы (`CreateDIBSection`, HDC, временные буферы масштабирования и I420) создаются однократно при инициализации или явной смене геометрии. На каждом кадре `malloc`/`free`, `CreateCompatibleDC`/`DeleteDC` запрещены.
   - **Парирование риска R3 (Утечка GDI-дескрипторов):** При получении масок курсора через `GetIconInfo` дескрипторы `hbmMask` и `hbmColor` **ОБЯЗАТЕЛЬНО** освобождаются через `DeleteObject` на каждом вызове.
4. **Принцип безопасности Fail-Closed и тайминги:**
   - Перед каждым захватом кадра и забором из конвейера выполняется проверка доступности активного рабочего стола через `OpenInputDesktop` (интервал проверки $\le 100$ мс).
   - При блокировке рабочего стола (Win+L), UAC, смене сессии или дисконнекте RDP: мгновенный возврат `L4C_ERR_SESSION_UNAVAILABLE`, остановка потока и сброс очередей $\le 500$ мс.
   - Передача черных заглушек, старых кадров или попытка автоматического возобновления захвата после разблокировки экрана **категорически запрещены**.

---

## 3. Pre-Flight Check & Contract Gate (Шаг 1)

Перед началом внесения изменений агент обязан выполнить валидацию входных контрактов:

1. Открой файл журнала `docs\l4capture\prompts\contract-handoff.md`.
2. Убедись, что блок `H-L4C-01-v1` присутствует в секции `## 5. Принятые handoff-блоки` и имеет статус `ACCEPTED`.
3. Сверь контрольные суммы входных артефактов из блока `H-L4C-01-v1`:
   - `docs/l4capture/handoffs/L4C-01-FRAMEWORK-report.md`: `ae06777610960c5fb4636eb21273332824de6abfe5d20815b34e7e17961c81c7`
   - `tools/l4capture/include/l4capture/types.h`: `97495e4e38cd99a7a2fcf70cf6db4e338aadda6e66e3bd6bb46d1f01266a08a1`
   - `tools/l4capture/include/l4capture/capture_backend.h`: `4c032b47bd80d8e3db07da785a03e4e0c4837f45f117234a5cbf8df017855c85`
   - `tools/l4capture/include/l4capture/encoder_backend.h`: `bc9f9a795e3779a89166e55f6ae0c1b1913e0e0c2cc4b500781de872db182986`
   - `tools/l4capture/include/l4capture/ipc_protocol.h`: `1f9970e01df1afbd85ed82dfd839b470543038981313d2fb16a3b3dec3a1e009`
   - `tools/l4capture/include/l4capture/safety_gate.h`: `7b0d133e0578f2ff70f72aa9f986f929895cc057526642e3df483b57aa7d342e`
   - `tools/l4capture/bin/x86/l4capture.exe`: `eb3154cca97966a114bac756de7a75513b0affee247d18ec9d7c53371e6d053e`
   - `tools/l4capture/bin/x64/l4capture.exe`: `8994459f794435b46d94431011489e8a6e0f6ff9c201b4124c4f6ba090ace729`
4. Проверь рабочую ветку Git: `l4capture/l4c-02-gdi-capture`.
5. При обнаружении несоответствий или повреждений заверши работу со статусом `BLOCKED_CONTRACT`.

---

## 4. Архитектурные требования и техническая спецификация

Реализуй компоненты захвата и видеоконвейера в строгом соответствии с C-интерфейсами, зафиксированными на шаге L4C-01:

### 4.1. GDI Capture Backend (`include/l4capture/gdi_capture.h`, `src/capture/gdi_capture.c`)

Реализует виртуальную таблицу `l4c_capture_backend_vtable_t`:

```c
#ifndef L4C_GDI_CAPTURE_H
#define L4C_GDI_CAPTURE_H

#include "capture_backend.h"

/* Фабричный метод создания экземпляра GDI capture backend */
l4c_status_t l4c_gdi_capture_create(l4c_capture_backend_t **out_backend);

#endif /* L4C_GDI_CAPTURE_H */
```

**Требования к реализации бэкенда (`gdi_capture.c`):**
1. **Инициализация (`init`):**
   - Получение дескриптора экрана (`hdc_screen = GetDC(NULL)`).
   - Создание совместимого контекста памяти (`hdc_mem = CreateCompatibleDC(hdc_screen)`).
   - Определение геометрии: если `config->target_rect` не задан (нулевой), захватывается весь виртуальный экран через системные метрики `SM_XVIRTUALSCREEN`, `SM_YVIRTUALSCREEN`, `SM_CXVIRTUALSCREEN`, `SM_CYVIRTUALSCREEN`. Если задан — используются физические координаты `target_rect`.
   - Проверка лимитов площади: площадь $W \times H \le 8\,294\,400$ пикселей (`L4C_MAX_PIXELS_AREA`). Превышение возвращает `L4C_ERR_OVERFLOW`.
   - Формирование `BITMAPINFO`: 32 бита (`biBitCount = 32`), `biCompression = BI_RGB`, top-down ориентация (`biHeight = -((LONG)height)`).
   - Создание DIB-секции: `CreateDIBSection(hdc_mem, &bmi, DIB_RGB_COLORS, (void**)&bits, NULL, 0)`.
   - `SelectObject(hdc_mem, hbmp)`.
   - Сохранение параметров: ширина, высота, положительный stride ($W \times 4$), указатель на буфер.
2. **Захват кадра (`acquire_frame`):**
   - Опрос доступности сессии: вызов `OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP)`. Если рабочий стол заблокирован или недоступен — немедленный возврат `L4C_ERR_SESSION_UNAVAILABLE`.
   - Копирование растра: вызов `BitBlt(hdc_mem, 0, 0, width, height, hdc_screen, origin_x, origin_y, SRCCOPY | CAPTUREBLT)`. Флаг `CAPTUREBLT` обязателен для захвата полупрозрачных и layered окон.
   - Наложение курсора: если `config->capture_cursor == true`, вызывается функция отрисовки курсора `l4c_cursor_draw(hdc_mem, &physical_rect)`.
   - Заполнение структуры `l4c_frame_view_t`:
     - `data` = указатель на DIB-буфер BGRA;
     - `width`, `height`, `stride` (положительный);
     - `buffer_size` = `height * stride`;
     - `physical_rect` = физические координаты (с поддержкой `left < 0`, `top < 0`);
     - `pts_ms` = текущее монотонное время `l4c_now_monotonic_ms()`;
     - `geometry_generation` = текущий номер поколения геометрии.
3. **Освобождение кадра (`release_frame`):**
   - Снятие блокировки кадра (в GDI DIB-буфер удерживается контекстом бэкенда до следующего цикла).
4. **Уничтожение бэкенда (`destroy`):**
   - Корректное освобождение ресурсов: `SelectObject(hdc_mem, old_hbmp)`, `DeleteObject(hbmp)`, `DeleteDC(hdc_mem)`, `ReleaseDC(NULL, hdc_screen)`.
   - Освобождение контекста структуры `l4c_capture_backend_t`.

### 4.2. Наложение аппаратного курсора (`include/l4capture/cursor.h`, `src/capture/cursor.c`)

```c
#ifndef L4C_CURSOR_H
#define L4C_CURSOR_H

#include "types.h"
#include <windows.h>

/* Отрисовка текущего аппаратного курсора на целевой контекст HDC */
l4c_status_t l4c_cursor_draw(HDC hdc_target, const l4c_rect_t *target_rect);

#endif /* L4C_CURSOR_H */
```

**Требования к отрисовке курсора (`cursor.c`):**
1. Вызов `CURSORINFO ci; ci.cbSize = sizeof(ci); GetCursorInfo(&ci);`.
2. Если `!(ci.flags & CURSOR_SHOWING)` — курсор скрыт системой, функция возвращает `L4C_OK` без отрисовки.
3. Запрос информации об иконке: `ICONINFO ii; GetIconInfo(ci.hCursor, &ii);`.
4. Расчет координат отрисовки с учетом точки привязки (hotspot) и смещения физического прямоугольника:
   ```c
   int draw_x = (int)(ci.ptScreenPos.x - target_rect->left - (int32_t)ii.xHotspot);
   int draw_y = (int)(ci.ptScreenPos.y - target_rect->top - (int32_t)ii.yHotspot);
   ```
5. Отрисовка: `DrawIconEx(hdc_target, draw_x, draw_y, ci.hCursor, 0, 0, 0, NULL, DI_NORMAL);`.
6. **КРИТИЧЕСКИЙ ШАГ (Zero-Leakage Invariant):**
   ```c
   if (ii.hbmMask) {
       DeleteObject(ii.hbmMask);
   }
   if (ii.hbmColor) {
       DeleteObject(ii.hbmColor);
   }
   ```
   *Запрещено оставлять дескрипторы иконки неосвобождёнными. Несоблюдение ведет к утечке GDI Objects и падению процесса.*

### 4.3. Билинейное масштабирование растра (`include/l4capture/scale.h`, `src/pipeline/scale.c`)

```c
#ifndef L4C_SCALE_H
#define L4C_SCALE_H

#include "types.h"

/* Билинейное масштабирование BGRA растра в целевое разрешение без обрезки */
l4c_status_t l4c_scale_bilinear_bgra(
    const uint8_t *src,
    uint32_t src_w,
    uint32_t src_h,
    int32_t src_stride,
    uint8_t *dst,
    uint32_t dst_w,
    uint32_t dst_h,
    int32_t dst_stride
);

#endif /* L4C_SCALE_H */
```

**Требования к масштабированию (`scale.c`):**
1. **Алгоритм:** Чистый C99 алгоритм билинейной интерполяции с целочисленной арифметикой фиксированной точки (16.16 fixed-point math). Float-вычисления в горячем цикле запрещены.
2. **Геометрия и пропорции:** Полное масштабирование всего исходного растра в выходной растр `854x480` (профиль `base_480p`) без обрезки (no crop) и без полей (no letterbox). На экранах отличных от 16:9 происходит пропорциональное растяжение/сжатие, гарантирующее сохранение взаимно однозначного маппинга нормализованных координат ввода.
3. **Интерполяция пикселей:** Вычисление весов для 4 соседних пикселей ($P_{00}, P_{10}, P_{01}, P_{11}$) и интерполяция каждого цветового канала (B, G, R, A) независимо:
   $$C = \frac{(P_{00}(1-x_f)(1-y_f) + P_{10}x_f(1-y_f) + P_{01}(1-x_f)y_f + P_{11}x_f y_f)}{65536}$$
4. **Валидация:** Проверка параметров через `limits.h`: $W, H > 0$, отсутствие переполнения, корректность положительных страйдов.

### 4.4. Цветовая конверсия BGRA $\to$ I420 (`include/l4capture/color_convert.h`, `src/pipeline/color_convert.c`)

```c
#ifndef L4C_COLOR_CONVERT_H
#define L4C_COLOR_CONVERT_H

#include "types.h"
#include "encoder_backend.h"

/* Контекст пула буферов цветовой конверсии */
typedef struct l4c_color_converter l4c_color_converter_t;

/* Создание конвертера с предварительным выделением плоскостей */
l4c_status_t l4c_color_converter_create(
    uint32_t width,
    uint32_t height,
    l4c_color_converter_t **out_converter
);

/* Преобразование BGRA top-down в планарный I420 (BT.601 limited range) */
l4c_status_t l4c_color_convert_bgra_to_i420(
    l4c_color_converter_t *converter,
    const uint8_t *bgra,
    int32_t bgra_stride,
    uint64_t pts_ms,
    l4c_raw_frame_t *out_raw
);

/* Уничтожение конвертера и освобождение памяти плоскостей */
void l4c_color_converter_destroy(l4c_color_converter_t *converter);

#endif /* L4C_COLOR_CONVERT_H */
```

**Требования к цветовой конверсии (`color_convert.c`):**
1. **Цветовая модель:** Строго **BT.601 limited range** (Studio Swing). Использование full-range (PC Swing) или формул BT.709 категорически запрещено (SDP зафиксирован под BT.601).
   - Формулы перевода целых чисел:
     $$Y = ((66 \times R + 129 \times G + 25 \times B + 128) \gg 8) + 16$$
     $$U = ((-38 \times R - 74 \times G + 112 \times B + 128) \gg 8) + 128$$
     $$V = ((112 \times R - 94 \times G - 18 \times B + 128) \gg 8) + 128$$
   - Насыщение (clamping):
     $$Y \in [16, 235], \quad U \in [16, 240], \quad V \in [16, 240]$$
2. **Субдискретизация хромы (Chroma Subsampling $2 \times 2$):** Значения $U$ и $V$ вычисляются как среднее арифметическое по блоку $2 \times 2$ пикселей исходного BGRA растра перед применением цветовой матрицы, исключая цветовую ступенчатость мелких элементов интерфейса.
3. **Выравнивание плоскостей и страйды:**
   - Для целевого растра `854x480`:
     - $Y$-плоскость: ширина 854, высота 480, `stride_y` выровнен по 16 байт ($854 \to 864$ байт);
     - $U$-плоскость: ширина 427, высота 240, `stride_u` выровнен по 16 байт ($427 \to 432$ байт);
     - $V$-плоскость: ширина 427, высота 240, `stride_v` выровнен по 16 байт ($427 \to 432$ байт).
   - Выравнивание страйдов критически важно для бесконфликтной передачи в кодер OpenH264 на шаге L4C-03.
4. **Управление памятью:** Конвертер аллоцирует непрерывные буферы под 2 слота `l4c_raw_frame_t` один раз в `create`. Никаких вызовов `malloc` во время обработки кадров.

### 4.5. Pacing-конвейер и latest-frame политика (`src/pipeline/pipeline.c`)

Развитие модуля конвейера, заложенного в L4C-01:
1. **Тактование (10 FPS):** Захват и передача осуществляются с интервалом $100$ мс по монотонному времени (`l4c_now_monotonic_ms()`).
2. **Политика latest-frame:** Очередь строго ограничена: ровно 1 слот в обработке и не более 1 слота в ожидании. Если потребитель (энкодер) занят обработкой кадра, поступающий свежий сырой кадр замещает ожидающий кадр в слоте (старый кадр отбрасывается без накопления очередей и задержки).
3. **Интеграция с Safety-Gate:** При срабатывании стоп-сигнала (`CMD_STOP`, истечение аренды, блокировка сессии) все буферы немедленно очищаются. Отправка остаточных кадров (drain) запрещена.

---

## 5. Структура файлов и обновлений в `tools/l4capture/`

```text
tools\l4capture\
├── include\
│   └── l4capture\
│       ├── gdi_capture.h        # [НОВЫЙ] Интерфейс GDI capture backend
│       ├── cursor.h             # [НОВЫЙ] Функции наложения аппаратного курсора
│       ├── scale.h              # [НОВЫЙ] Билинейный скейлер растра
│       ├── color_convert.h      # [НОВЫЙ] Конвертер BGRA -> I420 (BT.601 limited)
│       └── ... (ранее созданные заголовки L4C-01 остаются неизменными)
├── src\
│   ├── capture\
│   │   ├── gdi_capture.c        # [НОВЫЙ] Реализация GDI бэкенда (DIB, BitBlt, origin)
│   │   └── cursor.c             # [НОВЫЙ] Отрисовка курсора и очистка дескрипторов
│   ├── pipeline\
│   │   ├── scale.c              # [НОВЫЙ] Целочисленный билинейный масштабирующий алгоритм
│   │   ├── color_convert.c      # [НОВЫЙ] BT.601 limited конверсия и 2x2 субдискретизация
│   │   └── pipeline.c           # [ОБНОВЛЕНИЕ] Интеграция захвата, скейлера и I420 буферов
│   └── main.c                   # [ОБНОВЛЕНИЕ] Связывание GDI захвата с IPC циклом
├── tests\
│   ├── test_runner.c            # [ОБНОВЛЕНИЕ] Регистрация новых тестов
│   ├── test_gdi_capture.c       # [НОВЫЙ] Тесты GDI бэкенда и физических координат
│   ├── test_cursor.c            # [НОВЫЙ] Стресс-тест дескрипторов курсора (Zero GDI Leaks)
│   ├── test_scale.c             # [НОВЫЙ] Тесты интерполяции, сохранения краев и лимитов
│   └── test_color_convert.c     # [НОВЫЙ] Тесты SMPTE полос, BT.601 clamp и страйдов
├── build.cmd                    # [ОБНОВЛЕНИЕ] Включение новых модулей исходников
└── test.cmd                     # [ОБНОВЛЕНИЕ] Сборка и прогон расширенного набора тестов
```

---

## 6. Тестовая стратегия и автономная валидация (`tests/`)

Каждый реализованный компонент обязан быть покрыт бескомпромиссными модульными тестами:

1. **`test_gdi_capture.c` (Тесты GDI бэкенда):**
   - Создание, инициализация и уничтожение бэкенда.
   - Проверка переиспользования памяти: указатель `FrameView.data` не меняется между последовательными вызовами `acquire_frame`.
   - Проверка поддержки нестандартных физических прямоугольников, включая отрицательный origin (`left = -1920, top = 0`).
   - Проверка отказа при превышении площади 4K (`W * H > 8294400`) без краша.
2. **`test_cursor.c` (Тесты курсора и исключения утечек):**
   - Отрисовка курсора на синтетический контекст DIBSection.
   - Проверка вычисления координат курсора с учетом отрицательного origin.
   - **Стресс-тест утечек дескрипторов GDI (КРИТИЧЕСКИЙ ТЕСТ):**
     Выполнение $10\,000$ последовательных циклов `l4c_cursor_draw()`.
     Замер системных ресурсов до и после цикла через `GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)`.
     Разница между начальным и конечным количеством GDI-дескрипторов обязана быть **строго равна 0**.
3. **`test_scale.c` (Тесты билинейного масштабирования):**
   - Масштабирование синтетических паттернов: шахматная доска 1px, сплошная заливка, градиенты.
   - Проверка краевых пикселей: сохранение крайних левых/правых/верхних/нижних строк без выхода за границы буфера.
   - Масштабирование $1920 \times 1080 \to 854 \times 480$ и $3840 \times 2160 \to 854 \times 480$: отсутствие переполнения fixed-point регистров.
4. **`test_color_convert.c` (Тесты конверсии в I420):**
   - Синтетические SMPTE Color Bars: чистый белый (255,255,255), черный (0,0,0), красный (255,0,0), зелёный (0,255,0), синий (0,0,255).
   - Точная проверка ограниченного диапазона:
     - Для чистого белого: $Y = 235$, $U = 128$, $V = 128$;
     - Для чистого черного: $Y = 16$, $U = 128$, $V = 128$.
     - Проверка, что ни одно значение Y не выходит за границы $[16, 235]$, а U и V — за $[16, 240]$.
   - Проверка выравнивания страйдов: $864$ байт для Y, $432$ байта для U и V при ширине $854$.
5. **Проверка зависимостей и платформы (`dumpbin`):**
   - `dumpbin /dependents bin\x86\l4capture.exe` — только базовые системные Win32 DLL (`GDI32.dll`, `USER32.dll`, `KERNEL32.dll`, `ADVAPI32.dll`, `OLE32.dll`, `WS2_32.dll`).
   - Отсутствие внешних runtime-библиотек VC (`msvcr*.dll`, `vcruntime*.dll`).
   - Подсистема x86: Console 6.01.

---

## 7. Пошаговый алгоритм выполнения агентом (Agent Workflow)

Агент выполняет реализацию строго по следующим этапам:

1. **Этап 1: Contract Gate & Контроль допуска**
   - Проверить `docs/l4capture/prompts/contract-handoff.md` на наличие блока `H-L4C-01-v1` со статусом `ACCEPTED`.
   - Проверить совпадение контрольных сумм всех входных файлов.
2. **Этап 2: Заголовочные файлы интерфейсов**
   - Создать `include/l4capture/gdi_capture.h`, `include/l4capture/cursor.h`, `include/l4capture/scale.h`, `include/l4capture/color_convert.h`.
3. **Этап 3: Модуль наложения курсора и очистки дескрипторов**
   - Реализовать `src/capture/cursor.c`.
   - Создать и запустить тест `tests/test_cursor.c` со стресс-проверкой `GR_GDIOBJECTS`.
4. **Этап 4: Бэкенд захвата GDI**
   - Реализовать `src/capture/gdi_capture.c` с выделением DIBSection, поддержкой виртуального экрана, отрицательных координат и проверки `OpenInputDesktop`.
   - Реализовать модульный тест `tests/test_gdi_capture.c`.
5. **Этап 5: Билинейное масштабирование**
   - Реализовать `src/pipeline/scale.c` на целочисленной арифметике.
   - Реализовать модульный тест `tests/test_scale.c`.
6. **Этап 6: Цветовая конверсия BGRA $\to$ I420**
   - Реализовать `src/pipeline/color_convert.c` (BT.601 limited, $2 \times 2$ chroma average, выравнивание страйдов).
   - Реализовать модульный тест `tests/test_color_convert.c`.
7. **Этап 7: Интеграция видеоконвейера и связывание в `main.c`**
   - Обновить `src/pipeline/pipeline.c` для объединения шагов: захват $\to$ курсор $\to$ scale $\to$ I420 конверсия $\to$ pacing 10 FPS.
   - Обновить `src/main.c`.
8. **Этап 8: Сборка и сквозной прогон тестов**
   - Обновить `build.cmd` и `test.cmd`.
   - Собрать Release-версии x86 и x64 (`build.cmd all`). Убедиться в полном отсутствии предупреждений (/W4 Zero Warnings).
   - Запустить весь тестовый набор (`test.cmd`). Убедиться в 100% успехе (0 failures).
   - Проверить таблицу импортов через `dumpbin`.
9. **Этап 9: Подготовка отчёта и Candidate Handoff**
   - Сформировать отчёт `docs/l4capture/handoffs/L4C-02-GDI-CAPTURE-report.md`.
   - Рассчитать SHA-256 реальных байтов отчёта.
   - Сформировать кандидат `docs/l4capture/handoffs/L4C-02-GDI-CAPTURE-candidate.md` в формате `DETACHED_V1`.

---

## 8. Критерии приёмки (Definition of Done)

Шаг считается завершенным только при одновременном выполнении следующих условий:

1. **Компиляция и сборка:**
   - Бинарники `bin\x86\l4capture.exe` и `bin\x64\l4capture.exe` успешно собираются через `build.cmd` с флагом `/MT`.
   - `bin\l4capture.exe` идентичен `bin\x86\l4capture.exe`.
   - Компиляция проходит без единого предупреждения при уровне `/W4`.
2. **Отсутствие утечек дескрипторов (GDI Leak Proof):**
   - Стресс-тест курсора на $10\,000$ итераций подтверждает нулевой рост `GR_GDIOBJECTS`.
3. **Корректность цвета и геометрии:**
   - Преобразование SMPTE подтверждает соответствие диапазонов BT.601 limited range ($Y \in [16, 235]$, $U/V \in [16, 240]$).
   - Выравнивание страйдов плоскостей I420 кратно 16 байтам.
   - Отрицательные физические координаты экрана корректно захватываются и масштабируются.
4. **Pacing и безопасность:**
   - Тактование конвейера выдерживает 10 кадров/с по монотонным часам.
   - Проверка блокировки сессии (`OpenInputDesktop`) прекращает захват без выдачи зависших кадров или черных заглушек.
5. **Тестовое покрытие:**
   - Все тесты в `tests/` выполняются успешно через `test.cmd` (0 failures, 0 errors).
6. **Оформление handoff:**
   - Файлы `l4desk-service` не затронуты.
   - Отчёт и кандидат подготовлены строго в каталоге `docs/l4capture/handoffs/`.

---

## 9. Оформление отчёта и Candidate Handoff (`DETACHED_V1`)

По завершении всех работ сформируй два артефакта согласно стандарту `PROMPT-STANDARD.md`:

### 1. Отчёт исполнителя: `docs/l4capture/handoffs/L4C-02-GDI-CAPTURE-report.md`
Должен содержать:
- Статус: строго `ACCEPTED`.
- Ветка и SHA коммита реализации (`producer_commit`).
- Полный перечень созданных и изменённых файлов в `tools/l4capture/`.
- Вывод выполнения `build.cmd all` и `test.cmd`.
- Результаты замера дескрипторов GDI (`GR_GDIOBJECTS` до и после 10 000 вызовов).
- Вывод `dumpbin /dependents` для x86 и x64.
- Подтверждение параметров BT.601 limited range и выравнивания страйдов.

### 2. Файл кандидата: `docs/l4capture/handoffs/L4C-02-GDI-CAPTURE-candidate.md`
Вычисли SHA-256 хеш реальных байтов отчёта (PowerShell: `(Get-FileHash -Algorithm SHA256 docs\l4capture\handoffs\L4C-02-GDI-CAPTURE-report.md).Hash.ToLower()`).

Файл кандидата оформляется строго в формате:

```markdown
<!-- HANDOFF:H-L4C-02-v1:BEGIN -->
```yaml
handoff_id: H-L4C-02-v1
status: ACCEPTED
contract_kinds:
  - GDI_CAPTURE
  - CURSOR_OVERLAY
  - BILINEAR_SCALE
  - COLOR_CONVERT_I420
  - PIPELINE_PACING
producer_prompt_id: L4C-02-GDI-CAPTURE
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-02-GDI-CAPTURE-report.md
producer_branch: l4capture/l4c-02-gdi-capture
producer_commit: <GIT_COMMIT_SHA>
accepted_at_utc: <ISO_8601_TIMESTAMP>
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-02-GDI-CAPTURE-report.md
  - tools/l4capture/include/l4capture/gdi_capture.h
  - tools/l4capture/include/l4capture/cursor.h
  - tools/l4capture/include/l4capture/scale.h
  - tools/l4capture/include/l4capture/color_convert.h
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - <SHA256_REPORT>
  - <SHA256_GDI_CAPTURE_H>
  - <SHA256_CURSOR_H>
  - <SHA256_SCALE_H>
  - <SHA256_COLOR_CONVERT_H>
  - <SHA256_BIN_X86_EXE>
  - <SHA256_BIN_X64_EXE>
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
  breaking_changes: false
  notes: Реализация GDI-захвата, наложения аппаратного курсора без утечек дескрипторов, билинейного масштабирования и конверсии BGRA->I420 (BT.601 limited). Поддержка отрицательного origin, DPI, 10 FPS pacing.
deployment_status: LOCAL_BUILD_VERIFIED
deployed_environment: local_development
feature_flags:
  l4capture_native_pipeline: enabled
  l4capture_gdi_backend: enabled
contract_payload:
  capture_backend:
    type: GDI
    pixel_format: L4C_PIX_FMT_BGRA
    supported_origins: positive_and_negative
    dpi_awareness: physical_pixels
    resource_management: persistent_dc_and_dib_section
    cursor_handling: GetCursorInfo + DrawIconEx + DeleteObject(hbmMask, hbmColor)
  scaler:
    algorithm: Bilinear interpolation (integer fixed-point arithmetic)
    target_raster: 854x480 (base_480p)
    mode: stretch_fit (no crop, no letterbox)
  color_conversion:
    source_format: BGRA top-down
    target_format: I420 (3 planes: Y, U, V)
    standard: BT.601 limited range (Studio Swing, Y 16-235, U/V 16-240)
    chroma_subsampling: 2x2 box average
    stride_alignment: Y 16-byte, U/V 16-byte
  pacing_and_pipeline:
    target_fps: 10
    interval_ms: 100
    queue_depth: 1 processing, <= 1 pending (latest-frame semantics, drop oldest)
    session_guard: OpenInputDesktop check <= 100 ms, instant drop on lock/UAC
supersedes: []
known_risks:
  - R1: UAC/Lock — опрос сессии перед каждым кадром, прекращение потока без повтора старых кадров
  - R3: Утечки дескрипторов GDI — вызов DeleteObject для дескрипторов курсора гарантирован
  - R4: Отрицательный origin — корректное смещение координат в DIBSection и курсоре
consumers:
  - L4C-03-OPENH264-CODEC
  - L4C-04-RTP-SENDER
  - L4C-05-AGENT-ADAPTER
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
next_prompt_id: L4C-03-OPENH264-CODEC
```
<!-- HANDOFF:H-L4C-02-v1:END -->
```
