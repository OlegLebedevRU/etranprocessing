# Фото и кадры оборудования · 2026-10-09

Материалы пользователя: `D:/work/img/`. Исходники не изменялись.
Проверены обе записи промышленного шкафа, запись терминала доступа,
длинное видео вендинга и две фотографии. Для отбора сделаны контактные листы
с семью кадрами каждого видео; они находятся в игнорируемой `.preview/media-selection/`.

## Отобранные материалы

| Материал | Источник / момент | Подготовка | Применение |
| --- | --- | --- | --- |
| Панель L4-HMI | `l4-hmi_photo_2026-10-09_16-03-39.jpg` | Ретушь исходного фото: фон, перспектива, свет | Карточка L4-HMI |
| Плата IO-24 | `leo4-board-io24-hw.jpg` | Ретушь исходного фото: фон, ориентация, свет | Карточка L4 Board IO-24 |
| Промышленный шкаф | `l4_hmi_industrial_vending_impl_video_2.mp4`, 14.01 с | Исходный кадр, экспорт в WebP | Раскрываемая галерея PlaterraTerminal |
| Терминал доступа | `platerraterminal_locker_with_two_factors_auth_proximity&driverlicense_video_2026-10-09_16-06-02.mp4`, 13.18 с | Исходный кадр, экспорт в WebP | Та же галерея |
| Механизм выдачи | `Platerraterminal_vending_IMG_3938.MOV`, 55.64 с | Консервативная ретушь цвета и экспозиции | Та же галерея |

Оригинальные фотографии и отобранные JPEG-кадры сохранены в `originals/media/`.
Выбранные PNG-мастера — в `processed/`. Видео остаются в исходной папке;
их пути, размеры и SHA-256 сохранены в `media-input-manifest.json`.
Соответствие мастеров файлам сайта и размеры — в `media-output-manifest.json`.

Ретушь выполнена встроенным **imagegen**, режим **edit** с одним исходным
изображением на запрос. Варианты обработки шкафа и терминала доступа отклонены:
они меняли количество дверец или надписи интерфейса. Использованы исходные кадры.
Сохранённая обработка улучшает презентацию фотографии и не заменяет техническую
документацию. Полноразмерный оригинал доступен в материалах для сверки.

## Промпты выбранных вариантов

### IO-24

Edit the attached real photograph, do not create a redesigned product. Asset: accurate editorial product photograph for the Platerra website. Cleanly extract the entire green Leo4 IO-24 PCB from the wooden tabletop, straighten its perspective and rotate to a horizontal long-board orientation. Place it centered on a quiet warm off-white (#f4f5ef) surface with only a soft natural contact shadow; generous but restrained margins. Improve exposure, neutral white balance, reduce camera noise and compression, moderate true-detail sharpening. Preserve EXACT board geometry, every connector, number and order of connectors, all chips, traces, switches, buttons, mounting holes, original labels and colors. Do not add or delete components. Do not invent or redraw silkscreen text. Keep actual prototype appearance and defects; no glossy imagined industrial redesign. Landscape image. This is retouching an existing photo, not a render.

### L4-HMI

Retouch this real L4-HMI touchscreen photograph for an engineering product website. Preserve the exact physical display, black bezel, and screen pixels/content including ALL Russian text and the actual demo Mock SKU 02. Do not redesign the UI, regenerate typography, replace content, add controls, or invent details. Crop out the ruler, cables and wooden tabletop; straighten perspective so the panel is seen frontally, retain its portrait screen. Center the actual display on a warm off-white (#f4f5ef) quiet studio-like background with subtle contact shadow, in a landscape composition and ample side margins. Correct exposure/white balance and reduce glare only conservatively, mild noise reduction/sharpening. Keep it recognizable as the photographed engineering prototype. Do not make any screen words up. No extra text or graphic overlays.

### Механизм выдачи

Conservative restoration, ONLY COLOR/EXPOSURE adjustment of this real PORTRAIT vending-machine video frame. Neutralize the blue cast in the metal slightly while retaining the original photo, with mild noise reduction. Preserve the EXACT original crop, camera perspective, rows, divider plates, screws, slots, cables, package and all labels in their original positions. Do not expand to landscape or reconstruct a product. Do not alter or regenerate any label character: if letters are blurry, keep them blurry. No new text, objects, shelves or invented geometry. Keep original camera pixels and photographic imperfections as much as possible. Same portrait aspect, no new background.

## Экспорт и загрузка

`python scripts/pack-media.py` экспортирует мастера в WebP шириной до 480,
960 и 1280 px. Для повторного экспорта нужен Pillow; это отдельная операция
подготовки медиа, штатной Node-сборке Python не нужен. Скрипт только уменьшает
размер и меняет формат, без генеративной обработки.

Пять превью суммарно занимают около 115 KiB. У фото контроллеров есть responsive
srcset и lazy loading. Три кадра галереи не загружаются до её раскрытия.
Увеличение открывается по клику; без JavaScript доступны прямые ссылки.

## Проверка

Node-сборка и синтаксис проходят. Playwright: четыре затронутые страницы на
1440, 768, 390 и 320 px без горизонтального переполнения; 18 иконок;
фильтры 11 / 7 / 18; отсутствие загрузки кадров до раскрытия; загрузка всех
трёх превью; увеличение, Escape и восстановление фокуса; 18 проектов и
три прямые ссылки на кадры без JavaScript. Ошибок JavaScript не обнаружено.

Локальные скриншоты: `.preview/project-icons.png`, `.preview/controller-media.png`,
`.preview/prototype-media.png`. Публикация на сервер в этом изменении не выполнялась.
