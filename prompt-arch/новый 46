## Отчёт по тенантности UI-сервисов

### 1. ВАРИАНТЫ МЕНЮ (menu_variants) -- НЕ ТЕНАНТНЫ

**Проблема:** Таблица `menu_variants` не имеет колонки `org_id`. Все организации видят один и тот же пул вариантов.

- `MenuBuilder/backend/app/models.py:194-211` -- модель `MenuVariant` без `org_id`
- `MenuBuilder/backend/app/routers/menu_variants.py:15-18` -- `list_variants` возвращает ВСЕ варианты без фильтрации
- Уникальность имени -- глобальная (`name` unique), а не в рамках org

**Что нужно:** Добавить `org_id` в `menu_variants`, фильтровать по org_id во всех эндпоинтах.

---

### 2. РОУТЕРЫ МЕНЮ -- НЕТ АУТЕНТИФИКАЦИИ

Все CRUD-эндпоинты для вариантов, групп и услуг **не имеют аутентификации** вообще:

| Роутер | Файл | Эндпоинты без auth |
|--------|------|-------------------|
| `menu_variants.py` | строки 15-124 | GET, POST, DELETE, duplicate -- все 5 |
| `groups.py` | строки 13-88 | GET, POST, PUT, DELETE -- все 5 |
| `services.py` | строки 12-90 | GET, POST, PUT, DELETE, free-tsp -- все 6 |
| `terminal_bindings.py` | строки 109-156 | POST binding, DELETE binding |

Любой HTTP-запрос без JWT может читать, создавать, изменять и удалять данные меню любых организаций.

---

### 3. ГРУППЫ (groups) -- org_id НЕ ИЗ JWT

Таблица `groups` имеет колонку `org_id` (models.py:224), но:
- `create_group` берёт `org_id` из тела запроса, а не из JWT
- `list_groups` фильтрует только по `menu_variant_id`, не по `org_id`
- Фронтенд **хардкодит `org_id: 1`**:
  - `variants.tsx:197` -- `org_id: 1`
  - `GroupForm.tsx:42` -- `org_id: 1`

---

### 4. ОТЧЁТЫ -- ЧАСТИЧНО ТЕНАНТНЫ

| Отчёт | Auth | org_id фильтр | Статус |
|-------|------|---------------|--------|
| Payments (`/api/reports/payments`) | Да | Да (line 585) | OK |
| Balance by terminal | Да | Да (line 721) | OK |
| Balance by TSP | Да | Да (line 796) | OK |
| **Inkassation** (`/api/reports/inkass`) | **Нет** | **Нет** | **ПРОБЛЕМА** |
| Monitoring | Да | Да (line 206) | OK |

**Inkass report** (main.py:420-545): нет `Depends(get_current_user)`, нет фильтра по `org_id`. Таблица `tech_gate_records` не имеет `org_id` -- фильтрация возможна только через JOIN с `terminals` по `device_id`.

---

### 5. ПРИВЯЗКА ТЕРМИНАЛОВ (terminal_bindings)

- `list_terminals` (line 13) -- **тенантный**, фильтрует по `org_id` из JWT
- `create_or_update_binding` (line 109) -- **без auth**, любой может привязать любой терминал к любому варианту
- `delete_binding` (line 149) -- **без auth**

---

### 6. ListMenuFile И stats

- `ListMenuFile` (main.py:92) -- без auth, но это **намеренно** для терминалов. Резолвит вариант через `device_id` -> binding, что безопасно.
- `stats` (main.py:143) -- без auth, возвращает агрегаты по всем данным

---

### 7. СПРАВОЧНИКИ TSP-КОДОВ -- ОБЩИЕ (ПРАВИЛЬНО)

- Таблица `tsp` в ProcessingBackend -- глобальная, без `org_id` (models.py:200-207)
- `tsp_parameter_codes` -- глобальная (models.py:229-242)
- `terminal_types` -- глобальная (models.py:25-33)
- `services.tsp_code` -- целочисленный код, уникальный в рамках `menu_variant_id`

TSP-коды корректно являются общими справочниками.

---

### 8. MCP PIN СЕРВЕР -- НЕТ ТЕНАНТНОСТИ

Все инструменты MCP-сервера (`ProcessingBackend/mcp-pin-server/`) работают без org_id фильтрации:
- `list_terminals` -- все терминалы глобально
- `report_payments`, `report_balance_*`, `report_inkass` -- все отчёты глобально
- `generate_pin`, `revoke_pin` -- без проверки org

---

### СВОДКА: ЧТО НУЖНО ИСПРАВИТЬ

| Приоритет | Что | Где |
|-----------|-----|-----|
| **CRITICAL** | Добавить `org_id` в `menu_variants` | models.py, миграция |
| **CRITICAL** | Добавить auth во все роутеры меню | menu_variants.py, groups.py, services.py |
| **HIGH** | Добавить auth на POST/DELETE bindings | terminal_bindings.py |
| **HIGH** | Убрать хардкод `org_id: 1` из фронта | variants.tsx, GroupForm.tsx |
| **HIGH** | Добавить auth на stats endpoint | main.py |
| **MEDIUM** | Inkass report -- добавить auth + JOIN с terminals | main.py:420 |
| **LOW** | MCP PIN server -- добавить org scoping | mcp-pin-server/ |