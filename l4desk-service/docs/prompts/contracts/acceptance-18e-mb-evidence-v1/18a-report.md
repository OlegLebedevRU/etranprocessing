# L4D-18A-SHARED-FIX-01 — фиксация пакета общей схемы

Producer verdict: `ACCEPTED` для релизной фиксации `etranprocessing-db==0.1.1`.
Controller handoff status: `PENDING` до независимой записи
`H-L4D-18A-SHARED-v1` в журнале. Исторический `L4D-18A-SHARED.md` и
принятые блоки 04A/17F не изменялись.

## Task intake и contract gate

- Владелец пакета ORM: `shared/etranprocessing_db`. ProcessingBackend —
  единственный владелец Alembic; MenuBuilder и ProcessingBackend —
  потребители модели. Новых таблиц, миграций и runtime-переходов этот
  шаг не создаёт.
- Producer → Git source artifact → PB/MB. Контракт: 31 прежняя таблица
  регистрируется корневым импортом; ещё 23 модели L4Desk/IoT подключаются
  явным импортом `etranprocessing_db.l4desk`.
- Инварианты: исходный schema-v1, имена колонок/constraints/индексов,
  Python 3.14, thin declarative layer, одинаковая версия обоих
  потребителей, отсутствие разрушительного downgrade данных.
- Проверка: два принятых входа, все допустимые digest; source provenance
  20/20, wheel/sdist, метаданные/schema, полный shared pytest, Ruff,
  Pyright, неизменность потребительских lockfiles и публикация exact
  Git ref. Серверный deploy отсутствует.

Регистрация `R-L4D-18A-SHARED-FIX-01-v1` опубликована в
`4ed4c78d19d4d443da21a91c5fc714a57d2038c0`. Её prompt указывает
на `H-L4D-17F-DOCS-FIX-01-v1` и `H-L4D-04A-SHARED-v1`. Оба имеют ровно
один `ACCEPTED`, version 1.0.0, producer commits соответственно
`a4b98e95e1493c76f0fdfc409a6441065f8e7020` и
`537a1e493c83d1fa8e8cb765228be8d1b24a1d62`, не отозваны.
Контроллер независимо проверил 19/19 Git/raw artifact digest. В
разрешённом shared scope повторно проверены 3/3 артефакта 04A и один
адресно разрешённый 17F final report; остальные 17F файлы не читались.
Sequence gate — тот же принятый 17F FIX. Remote release ref при gate
совпал с `4ed4c78d...`. Исторический отсутствующий `H-L4D-17F-DOCS-v1`
не подставлялся вместо нового ID.

## Итоговая версия и публикация

**Повторно используется уже опубликованный Git-source package**:
`etranprocessing-db==0.1.1`, source commit
`537a1e493c83d1fa8e8cb765228be8d1b24a1d62`, каталог `shared`.
Место публикации — immutable Git ref
`https://github.com/OlegLebedevRU/etranprocessing/tree/537a1e493c83d1fa8e8cb765228be8d1b24a1d62/shared`.
Source manifest:
`shared/docs/l4desk/package-source-v011.json`, Git/raw SHA-256
`364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6`.

Все **20/20** source/config/lock/schema Git blobs в текущем release
checkout имеют точно те же SHA-256, что manifest из source commit;
`git diff` для этих файлов между source commit и текущим release HEAD
пуст. `pyproject.toml` и оба backend объявляют и фиксируют `0.1.1`;
обе `uv.lock` ссылаются на тот же editable путь `../../shared`.
По условию исходного 18A уже опубликованный идентичный пакет не требует
повторной публикации или увеличения версии. Такой способ доставки
явно принят 04A; `artifact-registry` здесь означает Git source ref.
Ни Python index, ни wheel/sdist не объявляются опубликованными.

Точные 20 source hash, версии обоих потребителей и результаты сборки
сохранены в [машинном протоколе](evidence/18a-release-checks.json).

## Проверки на чистом checkout

Среда: Windows, `uv 0.12.5`, Python 3.14.0. До проверки рабочее дерево
чистое; форматирование не изменило файлы пакета/тестов.

| Проверка | Команда/метод | Результат |
|---|---|---|
| Ruff lint | `uv run --locked ruff check --fix etranprocessing_db tests` | PASS, изменений нет |
| Ruff format | `uv run --locked ruff format etranprocessing_db tests` | PASS, 22 файла без изменений |
| Pyright | `uv run --locked pyright etranprocessing_db tests` | 0 errors, 0 warnings |
| Все shared tests | `uv run --locked pytest -q --basetemp .pytest_tmp/18a-20260927-1817` | **65 passed** |
| Build | `uv build --out-dir dist`, повтор в отдельный output dir | Wheel и sdist созданы, вторые файлы побайтно совпали с первыми |
| Метаданные | `test_package.py` и независимая проверка архива | Python ==3.14.*, только SQLAlchemy/asyncpg; 16 Python-файлов в wheel, схема в sdist, лишних gauge/env/handoff файлов нет |
| Декларативная схема | `test_l4desk_contract.py`, `test_l4desk_constraints.py` | Прежние 31 таблица неизменны; 23 opt-in модели, полный metadata snapshot равен `schema-v1.json` |

Первый вызов `uv run --locked pytest -q` дал `64 passed, 1 error`:
Windows отказал pytest в доступе к системному временному каталогу
`pytest-of-oleg_` во время setup теста упаковки. При повторе с
`--basetemp` внутри игнорируемого каталога проекта все 65 тестов
прошли. Исходный код и тест не менялись; первый прогон не называется
успешным.

Два текущих локальных build дали одинаковые SHA-256:

| Локальный verification artifact | SHA-256 |
|---|---|
| `etranprocessing_db-0.1.1-py3-none-any.whl` | `f092073efd8506297e450a0e7e928584c9f3a4c103594b44747814bb067f2be2` |
| `etranprocessing_db-0.1.1.tar.gz` | `de76e01fea8b8b8818e2226fa823a4ba9194446fba44314c3c343f62ae55815d` |

Эти локальные archive bytes отличаются от сохранённых локальных хешей
04A. Нормативный опубликованный artifact 04A — Git source manifest и
20 файлов, которые совпали. Новые wheel/sdist не стали другой
опубликованной версией и не используются потребителями. Сборка прошла
дважды, что подтверждает воспроизводимость текущего инструмента.

## Совместимость и rollback

- Source package и declarative metadata совпадают с принятым
  `H-L4D-04A-SHARED-v1`: `schema-v1.json` и `schema-v1.md` имеют прежние
  Git/raw digest. Нет изменения API импорта, зависимости, миграции,
  данных или флагов.
- PB и MB уже используют `0.1.1` в обоих lockfile. Проверка source
  package не равна повторной проверке running image и PostgreSQL;
  фактическую схему `027` и images проверял 17F. 18B/18E должны
  подтвердить точные работающие версии в своих rollout.
- Предыдущее source package `0.1.0` — исторический кандидат возврата
  **до** подключения 04A моделей. При уже применённой additive схеме
  `027` и импортирующих её потребителях самостоятельный downgrade
  shared до `0.1.0` опасен. Для текущего этапа точка восстановления —
  сохранённый immutable Git ref `0.1.1`; при проблеме будущего
  потребителя вернуть его image/config совместимо с `0.1.1`, не удалять
  таблицы или финансовую историю. Откат БД в 18A не выполнялся.
- 18B должен принять exact package ref и проверить своё исполнение;
  18E — то же для MenuBuilder. Их consumer-развёртывание не входит в 18A.

В отчёте нет новых секретов, терминалов, платежей или серверных файлов.
Проверка пакета завершена. Отдельный
[candidate](L4D-18A-SHARED-FIX-01-candidate.md) публикуется после этого
неизменяемого отчёта; до независимой приёмки контроллера 18B не открыт.
