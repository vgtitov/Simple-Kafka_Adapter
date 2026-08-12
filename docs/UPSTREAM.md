# Что и когда отправляем в апстрим

Форк живёт поверх [NuclearAPK/Simple-Kafka_Adapter](https://github.com/NuclearAPK/Simple-Kafka_Adapter).
Апстрим принимает вклад, но по своему графику, поэтому свои изменения мы сначала выпускаем в форке
(версия `X.Y.Z-fork.N`), а наверх отдаём отдельными PR — **по одной теме на PR**.

Этот файл — очередь: что уже влито, что готово к отправке, что осознанно не отправляем.
Обновлять при каждом релизе форка.

Это про **временные** дивергенции (появились → отправили → влито → разница исчезла сама).
Дивергенции, которые останутся навсегда даже после влития PR (например: у нас другой *default*
поведения) — отдельный реестр защиты от потери при слиянии, **[docs/FORK_DIVERGENCE.md](FORK_DIVERGENCE.md)**.
После любого `git merge upstream/main` — обязательный `python3 scripts/check_fork_divergence.py`.

---

## Правила

1. **Один PR — одна тема.** Автору должно быть понятно, что он берёт, без разбора чужого релиза.
2. **Ветка от `upstream/main`**, не от нашего `main`. Иначе в PR приезжает вся история форка.
3. **Ничего внутреннего.** Репозиторий публичный: без названий внутренних систем, топиков, модулей,
   регистров, адресов и имён сотрудников. Технику описываем нейтрально.
4. **Версию форка (`-fork.N`) в PR не тащим.** Версия — дело релиз-менеджера апстрима.
5. **Текст PR берём из `CHANGELOG.md`**: симптом → причина → что изменено → как проверить.
6. Спорные вещи (меняющие поведение по умолчанию) отправляем как **issue с вопросом**, а не как готовый PR.

### Как собрать такой PR

```bash
git fetch upstream
git checkout -b upstream/<тема> upstream/main
git cherry-pick <нужные коммиты>          # или git checkout <наш-коммит> -- <только нужные файлы>
# убрать из индекса всё, что относится к версии форка и к внутренним материалам
git push -u origin upstream/<тема>
```
Дальше PR из `vgtitov:upstream/<тема>` в `NuclearAPK:main`.

---

## Очередь

Состояние апстрима сверено с `upstream/main` (1.9.2, коммит `3df0a20`) построчно — колонка «что у автора
сейчас» содержит конкретные места, а не предположения.

| # | Тема | Файлы | Статус | Что у автора сейчас (проверено в 1.9.2) |
|---|---|---|---|---|
| 1 | **Выбор ветки union по схеме** — узел схемы приходит в `fillAvroFromJson()` параметром; ветка подбирается по типу JSON; отсутствующее поле получает `null`-ветку по схеме; убраны два разыменования `nullptr` | `src/avro_methods.cpp` | **влит** ([#84](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/84), апстрим 1.9.3) | Ветки жёстко захардкожены: `null` → `selectBranch(0)`, не-`null` → `selectBranch(1)`. Для `["string","null"]` и для union с тремя и более ветками уходит не та ветка. **Нашего SIGSEGV у автора НЕТ** — он появился в нашем форке вместе с подбором ветки; автору отдаём улучшение и две ловушки в довесок: недостижимый `case AVRO_UNION` с `value<GenericUnion>()` (`avro_methods.cpp:1370`) и `value<avro::null>()` (`:762`) — оба разыменовывают пустой `std::any` |
| 2 | **Сборка и документация**: скрипты, гейт avro-cpp >= 1.12.1, force-include `fmt/format.h`, overlay-триплет с `-fPIC`, `boost-container`, `.dockerignore`, переписанный `docs/building.md` | `scripts/*`, `CMakeLists.txt`, `.dockerignore`, `docs/building.md` | **отправлен** ([#81](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/81)), на ревью — 4 замечания автора (12.08), дорабатываем | В апстриме нет ни одного сборочного скрипта, а `docs/building.md` ведёт на avro-cpp 1.12.0 — версию с багом декодирования рекурсивных схем. Подробное обоснование — в тексте PR ниже |
| 3 | **base64 на входе декода** — явный параметр `IsBase64` у `DecodeAvroMessage`/`GetAvroSchema` (`false` по умолчанию = поведение не меняется) | `src/utils.cpp/h`, `src/avro_methods.cpp`, `src/SimpleKafka1C.cpp/h` | **отправлен** ([#88](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/88)) | Автор в [issue #87](https://github.com/NuclearAPK/Simple-Kafka_Adapter/issues/87) выбрал вариант 2 (явный флаг) — угадывание отклонено как «закрытый ящик»: сырое тело теоретически может целиком состоять из символов алфавита base64. У автора декодирования base64 нет вообще (в `utils.cpp` только `base64Encode` для выдачи тела наружу), поэтому PR — чистая добавка без эвристики: не передан/`false` — как сейчас, `true` — декодировать, при невалидном base64 явная ошибка. В форке (`1.9.3-fork.1`) параметр tri-state через `Неопределено`, чтобы сохранить прежнюю эвристику по умолчанию для существующих вызовов — в апстриме этой обратной совместимости поддерживать не от чего, поэтому там проще: обычный `bool = false` |
| 4 | **Порча памяти в `consume()`** — тело сообщения берётся как `std::string(payload, len)` вместо `slice()` | `src/consumer_methods.cpp` | **влит** ([#82](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/82), апстрим 1.9.3) | `consumer_methods.cpp:444` зовёт `slice(payload, 0, msg->len())`, а `slice()` (`utils.cpp`) копирует байты `[0..to]` включительно и дописывает `s[j] = 0` — то есть **читает `payload[len]` и пишет `payload[len+1]`, за границу буфера librdkafka** |
| 5 | **Ресурсы и потокобезопасность**: `unique_ptr` для `RdKafka::Message`, мьютекс на `rebalance_cb.offsets`, upsert и проверка типов в `setParameter`/`setPartitioner`, проверки типов в `produce()` | `src/consumer_methods.cpp`, `src/SimpleKafka1C.cpp/h`, `src/producer_methods.cpp` | **влит** ([#83](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/83) — unique_ptr/мьютекс, [#85](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/85) — upsert/проверка типов; оба в апстрим 1.9.3) | `consume`/`getMessage`/`consumeBatch` (`:409`, `:481`, `:575`) держат сырой `RdKafka::Message*` с ручным `delete` — утечка на исключении; в admin-методах и `readMessageByOffset` автор уже перешёл на `unique_ptr`, то есть направление его же. `offsets` ребаланса без мьютекса. `setParameter` (`SimpleKafka1C.cpp:584`) — `push_back` без проверки типа и без upsert, повторные вызовы копят дубли |
| 6 | **Обновление схемы применяется**: кэш текста схемы в `putAvroSchema` / `putProtoSchema` | `src/avro_methods.cpp`, `src/protobuf_methods.cpp` | **влит** ([#86](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/86), апстрим 1.9.3) | `putAvroSchema` компилирует схему, только если имени ещё нет в `schemesMap` — повторный вызов с изменённым текстом молча игнорируется, в переиспользуемом экземпляре застревает старая схема. В `putProtoSchema` то же самое (`skip if exists`). Наша версия кэширует текст: тот же текст — выход, изменённый — перекомпиляция |
| 7 | Постмортем `docs/POSTMORTEM-avro-base64-decode.md` | — | **не отправляем** | Внутренний разбор инцидента. В апстрим уходит только вывод — в виде кода и раздела в `CHANGELOG` |

---

## Заготовка текста PR №2 (сборка и документация)

> **Почему это стоит взять.** Сейчас собрать компоненту по `docs/building.md` можно только методом проб:
>
> 1. Инструкция предлагает `avro-1.12.0`. На версиях ниже 1.12.1 декодирование глубоко вложенной
>    рекурсивной схемы падает (`vector::_M_range_check` / segfault) — воспроизведено на 1.11.3 и
>    исправлено в 1.12.1. То есть по инструкции собирается заведомо дефектный бинарь.
> 2. В списке пакетов Windows (`docs/building.md`, строки 11–18) нет `fmt`, `curl` и `boost-container`,
>    хотя `CMakeLists.txt` их требует явно — `find_package(fmt CONFIG REQUIRED)` (строка 115),
>    `find_package(CURL REQUIRED)` (147), `find_package(Boost REQUIRED COMPONENTS json container)` (124).
>    Конфигурация CMake по инструкции просто не проходит.
> 3. Зависимость от того, какой заголовок fmt подтянет avro-cpp: `<avro/Exception.hh>` зовёт
>    `fmt::format()`. На avro-cpp 1.12.1 + fmt 12.1 проблемы нет (`core.h` тянет `format.h`), но на других
>    сочетаниях версий в цепочку попадает только базовый заголовок — force-include это снимает.
> 4. Для Linux нет overlay-триплета с `-fPIC` — статические зависимости не линкуются в `.so`.
> 5. Правка пути к vcpkg прямо в `CMakeLists.txt` (как советует инструкция) означает локальную правку
>    файла проекта у каждого сборщика. Всё это выражается через `CMAKE_TOOLCHAIN_FILE` и
>    `VCPKG_TARGET_TRIPLET`.
> 6. Сборочных скриптов в репозитории нет, а Linux-сборка на практике удобнее в Docker.
>
> **Что в PR:** `scripts/build_windows.bat`, `scripts/build_linux.{sh,bat}`, `scripts/Dockerfile.ubuntu20`,
> overlay-триплет, `.dockerignore`, автономные проверки Avro в `scripts/avro_selftest`, поправки
> `CMakeLists.txt` (только force-include fmt — методов форка в этом PR нет) и переписанный
> `docs/building.md` — по шагам, с проверкой после каждого и с разделом «грабли и почему так».

---

## История

| Дата | PR | Итог |
|---|---|---|
| 2026-06-23 | [#80](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/80) «Проброс текста ошибок librdkafka в 1С (GetLastError)» | Влит 28.06.2026, вошёл в апстрим 1.9.2. Автор поверх добавил свою чистку `msg_err` в best-effort шагах |
| 2026-07-27 | [#81](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/81) Воспроизводимая сборка: скрипты, гейт avro-cpp, Docker, переписанная `docs/building.md` | открыт, ветка `upstream/build-docs` |
| 2026-07-27 | [#82](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/82) `consume()`: запись за границу буфера librdkafka | Влит 28.07.2026, вошёл в апстрим 1.9.3 |
| 2026-07-27 | [#83](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/83) Консьюмер: `unique_ptr` на сообщения, мьютекс на offsets ребаланса | Влит 28.07.2026, вошёл в апстрим 1.9.3 |
| 2026-07-27 | [#84](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/84) AVRO: ветка union по схеме вместо индексов 0/1 | Влит 28.07.2026, вошёл в апстрим 1.9.3 |
| 2026-07-27 | [#85](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/85) Настройки: upsert по ключу, понятная ошибка вместо `bad_variant_access` | Влит 28.07.2026, вошёл в апстрим 1.9.3 |
| 2026-07-27 | [#86](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/86) Обновление схемы применяется (кэш текста в `PutAvroSchema`/`PutProtoSchema`) | Влит 28.07.2026, вошёл в апстрим 1.9.3 |
| 2026-07-27 | [#87](https://github.com/NuclearAPK/Simple-Kafka_Adapter/issues/87) issue: 1С иногда передаёт тело как base64 — как правильнее обработать | Автор ответил 28.07.2026: вариант 2 (явный флаг). PR — см. следующую строку |
| 2026-08-12 | [#88](https://github.com/NuclearAPK/Simple-Kafka_Adapter/pull/88) Явный параметр `IsBase64` у `DecodeAvroMessage`/`GetAvroSchema`, без эвристики, `false` по умолчанию | открыт, ветка `upstream/base64-explicit-flag`, комментарий в #87 |

Форк синхронизирован с апстрим `1.9.3` (2026-08-12): PR #82–#86 в апстриме и в форке — один и тот же код,
расхождений не осталось.
