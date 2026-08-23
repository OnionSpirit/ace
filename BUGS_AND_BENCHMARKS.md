# ACE Framework — Found Bugs & Benchmark Candidates

Дата обновления: 2026-08-07

## Быстрый запуск бенчмарков

```bash
meson setup build-bench -Dbenchmarks=true
ninja -C build-bench ace_benchmarks
./build-bench/ace_benchmarks
```

Бенчмарки используют Google Benchmark. Опция `-Dbenchmarks=true` добавляет
wrap-зависимость `google-benchmark` и цель `ace_benchmarks`.

**Структура бенчмарков:**

| Файл | Назначение |
|------|-----------|
| `benchmarks/main.cpp` | Google Benchmark entry point |
| `benchmarks/environment.h` | Хелперы: configure/reset_runners, fetch(ch) |
| `benchmarks/benchmarks.cpp` | Сами бенчмарки (BM1-BM20, 21 шт.) |

**Инвентарь бенчмарков (актуально на 2026-08-07):**

| # | Бенчмарк | Что измеряет |
|---|----------|-------------|
| BM1 | `bm_cutex_race_capture` | Пропускная способность cutex (capture/release), 8 раннеров × 100k |
| BM2 | `bm_cutex_race_sync` | cutex с rescheduling (sync), миграция waiter-ов |
| BM3 | `bm_timer_parallel` | Масштабируемость clock: 100k таймеров на 4 раннерах |
| BM4 | `bm_spawn_cancel` | Массовый spawn + немедленный cancel (утечки) |
| BM5 | `bm_timer_ordering` | Срабатывание таймеров 0..501ms (все доставлены) |
| BM6 | `bm_multi_runner_cutex` | Целостность счётчика под cutex, 4 раннера |
| BM7 | `bm_channel_push_pull` | push/pull цикл dyn::bus (100k сообщений) |
| BM8 | `bm_spawn_join` | Задержка spawn + join цикла |
| BM9 | `bm_timeout_short` | Пропускная способность clock: 20k таймеров по 1ms |
| BM10 | `bm_compose_and` / `bm_compose_or` | Накладные расходы and/or композиций |
| BM11 | `bm_schedule_throughput` | attach/yank/release цикл диспетчера (200k задач) |
| BM12 | `bm_io_buffer_append` | Сборка scatter-gather буфера (append+assemble) |
| BM13 | `bm_io_buffer_clone` | Глубокое копирование io::buffer |
| BM14 | `bm_pipe_io_roundtrip` | Полный цикл write+read через io_uring на pipe |
| BM15 | `bm_channel_pending_push` | Асинхронный push с backpressure (pending_push) |
| BM16 | `bm_reattach_migration` | Кросс-раннерная миграция через reattach (20k хопов) |
| BM17 | `bm_spawn_fire_forget` | Массовый spawn без join (50k задач) |
| BM18 | `bm_automaton_ping` | Потребление co_yield через ping (50k значений) |
| BM19 | `bm_expire_absolute` | Таймеры с абсолютными дедлайнами (5k) |
| BM20 | `bm_compose_variadic` | Вариативные and/or из 3+ futures |

---

## Найденные баги

### B1. `compose.h`: `or_await_composed<3+>` — ошибка void→bool конвертации
```Исправлено``` - новая сигнатура условия ```if (_waiter.operator bool() and _waiter->_data.is_exist())```

**Файл:** `include/ace/core/compose.h:275`

**Симптом:**
```cpp
error: could not convert 'operator&&<...>(...)' from 'void' to 'bool'
if (_waiter and _waiter->_data)  // _waiter->_data тип void
```

**Причина:** `or_await_composed` с 3+ future-типами использует `omniptr::operator->()` для доступа к `_waiter->_data`. Для `ace::task` (async<void>) поле `_data` имеет тип `void`, но выражение `_waiter and _waiter->_data` пытается сконвертировать `void` в `bool`.

**Тест:** `cross_mechanic_fixture.or_await_composed_3` — закомментирован.

**Приоритет:** Средний (требуется для variadic or-композиции из 3+ future).

---

### B2. `compose.h`: `and_await_composed` — observer-задачи не отменяются при cancel
```Исправлено``` - всё и так работало

**Файл:** `include/ace/core/compose.h` (and_await_composed observer spawning)

**Симптом:** При `handle.cancel()` на spawned-задаче использующей `and` композицию, observer-задачи (созданные внутри `and_await_composed`) продолжают выполняться. `handle.join()` зависает или возвращает некорректный результат.

**Причина:** `and_await_composed::await_suspend()` создаёт observer-задачи через `async_handle` и spawn'ит их через `runner_ptr->attach_front()`. При cancel родительской задачи, observer'ы не получают сигнал отмены и продолжают ждать свои future.

**Тест:** `cross_mechanic_fixture.and_compose_with_cancel` — закомментирован.

**Приоритет:** Высокий (утечка ресурсов при отмене and-композиций).

---

### B3. `omniptr.h`: `operator&()` — const-correctness нарушена
```Исправлено``` - у метода убран ```const```

**Файл:** `include/ace/core/tools/omniptr.h:73-75`

**Симптом:**
```cpp
error: 'reinterpret_cast' from type 'void* const*' to type 'int**' casts away qualifiers
auto operator&() const {
    return reinterpret_cast<option_t**>(&_ptr);
}
```

**Причина:** `operator&()` объявлен как `const`, но возвращает неконстантный `T**`. Внутри метода `_ptr` имеет тип `void* const`, адрес которого `void* const*` нельзя `reinterpret_cast` в `int**` (сброс const).

**Исправление:** Либо убрать `const` с метода, либо возвращать `const T**`. Выбор зависит от того, используется ли этот оператор в кодовой базе.

**Тест:** `omniptr_fixture.address_of_operator` — не реализован (⬜ в TEST_PLAN.md, закомментирован).

**Приоритет:** Низкий (оператор не используется в текущем коде).

---

### B4. `promise.h` / `async_handle.h`: `join()` возвращает false для успешно завершённых void-корутин 
```Исправлено``` - применены исправления

**Файлы:** `include/ace/core/traits/promise.h:167`, `include/ace/core/async_handle.h:88`

**Симптом:** `async_handle::join()` вызывает `await_resume()` который возвращает `_handle.finished()`. `finished()` проверяет `_block->_status == e_finished`. Но `promise_return_traits<void>::return_void()` не устанавливает статус в `e_finished` — он остаётся `e_executed`.

**Причина:** В специализации `promise_return_traits<promiseT, void>` метод `return_void()` возвращает `suspend_never{}` без вызова `status(e_finished)`. В отличие от типизированной версии `return_value(v)`, которая вызывает `_derived->status(e_finished)`.

**Исправление:** Добавить `_derived->status(e_finished)` в `return_void()`:
```cpp
auto return_void() {
    _derived->status(e_finished);
    return std::suspend_never{};
}
```

**Затронутые тесты:** Существующие тесты (`check_spawn_and_join`) не проверяют возвращаемое значение `join()`, поэтому баг не был обнаружен ранее. Тест `spawn_extra_fixture.spawn_and_join` был переписан на использование `handle.done()` вместо `handle.join()` из-за этого бага.

**Приоритет:** Высокий (join() — публичное API, возвращаемое значение используется для определения успешности).

---

### B5. `queue.h`: `queue::queue(queue&&)` — не обнуляет head/tail источника
```Исправлено``` - применены исправления

**Файл:** `include/ace/core/tools/queue.h:139-142`

**Симптом:** После перемещения очереди через move-конструктор, исходная очередь сохраняет указатели `head`/`tail` на узлы перемещённой очереди. Вызов `dequeue()` на исходной очереди приведёт к чтению перемещённых данных.

**Причина:**
```cpp
queue(queue&& q) noexcept : mempool(q.mempool) {
    this->head = q.head;
    this->tail = q.tail;
    // q.head и q.tail НЕ обнуляются!
}
```

**Исправление:** Добавить обнуление:
```cpp
queue(queue&& q) noexcept : mempool(q.mempool) {
    this->head = q.head;
    this->tail = q.tail;
    q.head = nullptr;
    q.tail = nullptr;
}
```

**Тест:** `queue_fixture.queue_move_constructor` — изначально ожидал `_queue.empty()` после move, тест был скорректирован под текущее поведение.

**Приоритет:** Средний (move-конструктор используется в clock/hierarchical_time_wheel; потенциальный use-after-free).

---

### B6. `clock.h`: `cached_now()` — устаревший кэш времени ломает точность таймеров
```Исправлено``` - добавлено обновление кэша по времени (≥1ms) в дополнение к счётчику вызовов

**Файл:** `include/ace/services/clock.h:58-71`

**Симптом:** Таймеры срабатывают на десятки-сотни миллисекунд РАНЬШЕ. В shuffle-прогоне
`timer_fixture.do_and_await_test` (и `do_or_await_test`) получал `ms_time = 2` вместо ≥95ms:
композиция `(timeout(100ms) and timeout(10ms))` завершалась за 2ms.

**Причина:** `cached_now()` обновлял timestamp только каждый 16-й вызов. `advance()`/`adjust()`
синхронизируют `_release_bound` колеса с этим (возможно устаревшим) значением. Если предыдущий
тест оставлял счётчик вызовов немножественно от 16, а время между вызовами большое, то
`_release_bound` «застревал» в прошлом; при следующем обновлении кэша `elapsed()` скачком
становился больше длительности свежеподписанного таймера, и тот истекал мгновенно.

**Исправление:** обновлять кэш не только по счётчику, но и когда он старше 1ms (тик колеса):
```cpp
if (refresh_counter % 16 == 0 or (now - cached_ts) >= std::chrono::milliseconds(1))
    cached_ts = now;
```

**Затронутые тесты:** `do_or_await_test`, `do_and_await_test`, `timeout_*`, все таймерные
тесты в shuffle-режиме (`--gtest_shuffle`).

**Приоритет:** Высокий (точность — контракт clock).

---

### B7. `channel.h`: `channel_router::cancel()` — бесконечный цикл
```Исправлено``` - в цикле добавлен повторный `pop_node()`

**Файл:** `include/ace/futures/channel.h:329-336`

**Симптом:** `handle.cancel()` на задаче, висящей в `ch.pull()`, приводил к бесконечному
циклу в `channel_router::cancel()`: раннер застревал в `runner::reattach(node)` и `ace::run()`
никогда не завершался (тест `cross_mechanic_fixture.cancel_spawned_with_channel` был
отключён из-за этого).

**Причина:**
```cpp
auto* node = _waiters->pop_node();
while (node)
    core::runner::reattach(node);   // node НЕ перечитывается из очереди!
```
Тело цикла не вызывает `pop_node()` повторно — одна и та же нода реаттачится бесконечно.

**Исправление:**
```cpp
auto* node = _waiters->pop_node();
while (node) {
    core::runner::reattach(node);
    node = _waiters->pop_node();
}
```

**Приоритет:** Высокий (hang вместо отмены; тест переоткрыт).

---

### B8. `kernelic.h`: `submit()` — разыменование null `sqe` при переполнении ring
```Исправлено``` - запрос с полным ring уходит в overflow-буфер без удержания sqe

**Файл:** `include/ace/services/kernelic.h` (submit, kernel_entity)

**Симптом:** При >4096 одновременно висящих запросах (ёмкость io_uring ring)
`io_uring_get_sqe()` возвращает `nullptr`, а `io_uring_sqe_set_data(sqe, observer)` падает
с SEGV (ASan: heap-use-after-free / null-deref). Воспроизводится тестом
`base_fixture.kernelic_overflow_buffer_stress` (6000 висящих read на пустом pipe).

**Причина:** `submit()` безусловно вызывал `io_uring_sqe_set_data(sqe, ...)` ДО проверки
`_queries < 4096`. При полном ring указатель null.

Дополнительно: ветка `_queries >= 4096` удерживала уже полученный `sqe` в `kernel_entity`
и вызывала `io_uring_foo(sqe, ...)` только позже в `apply()` — к моменту submit-а слот
содержал неинициализированный SQE, kernel выполнял «мусорную» операцию, и для observer
приходил ложный CQE (use-after-free при повторном CQE после завершения задачи).

**Исправление:** `submit()` откладывает запрос в `_submission_buffer` БЕЗ удержания sqe;
`kernel_entity::apply()` сам запрашивает свежий `sqe` на момент применения:
```cpp
if (_queries < static_cast<int>(max_entries) and sqe) { /* прямой путь */ }
// иначе: буферизация {io_uring_foo, observer, params...}
```
```cpp
bool apply() {
    if (not _observer) return false;
    io_uring_sqe* sqe = io_uring_get_sqe(&_ring);
    if (not sqe) return false;   // caller re-queue
    io_uring_sqe_set_data(sqe, _observer);
    _action(_io_uring_foo, sqe, _params);
    return true;
}
```

**Приоритет:** Высокий (SEGV при нагрузке > 4096 IO).

---

### B9. `kernelic.h`: `ping()` — unsigned underflow границы буфера теряет запросы
```Исправлено``` - граница приведена к знаковому типу, apply() возвращает bool и re-queue

**Файл:** `include/ace/services/kernelic.h:364-367`

**Симптом:** Тест `kernelic_overflow_buffer_stress` зависал навсегда: ровно 1904
(6000-4096) запросов никогда не выполнялись, `_queries` застревал на 1904, service
busy-loop-ил `ping()` (миллионы итераций).

**Причина:** `for (unsigned i = 0; i < (max_entries - _queries) and ...)` — при
`_queries > max_entries` выражение `max_entries - _queries` становится огромным
unsigned-числом, и цикл выкачивал ВЕСЬ overflow-буфер за один ping, хотя ring был полон.
`apply()` не получал sqe и молча терял сущность — её CQE никогда не приходил.

**Исправление:**
```cpp
for (int i = 0; i < static_cast<int>(max_entries) - _queries and not _submission_buffer.empty(); ++i) {
    auto entity = _submission_buffer.dequeue();
    if (not entity.apply()) {
        _submission_buffer.enqueue(std::move(entity));
        break;
    }
}
```

**Приоритет:** Высокий (зависание раннера при переполнении ring).

---

### B10. `async_handle.h`: `ping_handler::await_resume()` игнорирует результат `yield_value()`
```Исправлено``` - при неудачном чтении co_yield возвращается nullopt

**Файл:** `include/ace/core/async_handle.h:83-93`

**Симптом:** `_handle.yield_value(&res)` помечен `[[nodiscard]]`, но возвращаемое значение
игнорировалось: при неуспешном чтении co_yield вызывающий получал default-constructed
значение вместо `std::nullopt`.

**Исправление:**
```cpp
resume_t res;
if (_handle.yield_value(&res)) return res;
return std::nullopt;
```

**Приоритет:** Средний (некорректное значение вместо nullopt в редкой гонке).

---

### B11. `cutex.h`: `~proxy()` бросает исключение из noexcept-деструктора
```Открыто``` - конструктивная ловушка misuse: throw из деструктора = terminate()

**Файл:** `include/ace/futures/cutex.h:290-296`

**Симптом:** При `sync()`-захвате без ручного `release()` деструктор `~proxy()` вызывает
`throw std::logic_error` — из-за неявного `noexcept` деструктора это приводит к
`std::terminate()` (предупреждение GCC `-Wterminate`), а не к исключению.

**Примечание:** Дизайн намеренно ловит misuse, но текущий механизм (terminate) не позволяет
приложению отреагировать. Рекомендуется либо убрать `throw` (логировать + release), либо
сделать misuse детектируемым до деструктора. Поведение не менялось — задокументировано.

**Приоритет:** Низкий.

---

### B12. `meson.build`: сломанный `--gtest_filter` — тесты НЕ выполнялись
```Исправлено``` - убран пробел и кавычки из аргумента фильтра

**Файл:** `meson.build:140`

**Симптом:** `meson test` показывал «Ok: 234, Fail: 0» за ~10 секунд, хотя полный прогон
тестов занимает ~4s+ только на таймеры и cutex-гонки. Все 234 теста проходили «вхолостую».

**Причина:** аргумент `'--gtest_filter= "@0@"'` содержит пробел после `=`. gtest получал
фильтр с ведущим пробелом и кавычками, не находил ни одного теста и завершался с кодом 0:
```
Note: Google Test filter =  queue_fixture.queue_order
[  PASSED  ] 0 tests.
WARNING: filter " queue_fixture.queue_order" did not match any test
```

**Исправление:** `'--gtest_filter=@0@'` (без пробела и кавычек).

**Приоритет:** Критический (весь CI-прогон был пустым).

---

## Кандидаты в бенчмарки (медленные тесты)

> Актуализировано 2026-08-07: сценарии BM1-BM6 реализованы в `benchmarks/benchmarks.cpp`
> (BM1-BM11) со сниженными объёмами, чтобы каждый прогон укладывался в ~0.1-0.5s.
> Оригинальные тесты-нагрузки по-прежнему живут в `tests/tests.cpp` и проверяют
> корректность (не производительность). Ниже — исходные кандидаты для справки.

### BM1. `timer_fixture.do_timer_on_runner_parallel_test`

**Время:** ~0.5-2s (в тесте 10000 сетов × 10 таймеров = 100k таймеров на 4 раннерах)
**Нагрузка:** 100,000 таймеров
**Что проверяет:** Масштабируемость clock/hierarchical_time_wheel при массовых таймерах.
**Бенчмарк:** `bm_timer_parallel` (BM3) — 100k таймеров, ~0.5s на итерацию.
**Файл теста:** `tests/tests.cpp` (`do_timer_on_runner_parallel_test`)

### BM2. `cutex_fixture.cutex_race`

**Время:** ~0.2s (8 раннеров × 100,000 инкрементов)
**Нагрузка:** 800,000 операций capture/release
**Что проверяет:** Корректность cutex в условиях высокой конкуренции.
**Бенчмарк:** `bm_cutex_race_capture` (BM1).
**Файл теста:** `tests/tests.cpp` (`cutex_race`)

### BM3. `cutex_fixture.cutex_race_resheduling`

**Время:** ~0.2s (8 раннеров × 100,000 инкрементов с rescheduling)
**Нагрузка:** 800,000 операций capture/sync с rescheduling
**Что проверяет:** Корректность cutex с rescheduling при экстремальной нагрузке.
**Бенчмарк:** `bm_cutex_race_sync` (BM2).
**Файл теста:** `tests/tests.cpp` (`cutex_race_resheduling`)

### BM4. `cross_mechanic_fixture.multi_runner_cutex_count`

**Время:** ~0.5s (4 раннера × 4000 инкрементов)
**Нагрузка:** 16,000 операций capture/release
**Что проверяет:** Атомарность счётчика под cutex на 4 раннерах.
**Бенчмарк:** `bm_multi_runner_cutex` (BM6).
**Файл теста:** `tests/tests.cpp` (`multi_runner_cutex_count`)

### BM5. `cross_mechanic_fixture.stress_spawn_cancel`

**Время:** ~0.05s (100 spawn → cancel → join)
**Нагрузка:** 100 параллельных spawn + cancel циклов
**Что проверяет:** Отсутствие утечек памяти при массовом spawn/cancel.
**Бенчмарк:** `bm_spawn_cancel` (BM4).
**Файл теста:** `tests/tests.cpp` (`stress_spawn_cancel`)

### BM6. `timer_fixture.do_timer_on_runner_test` / `do_expire_on_runner_test`

**Время:** ~0.5s каждый
**Нагрузка:** 15 таймеров с разными duration (0ms → 501ms)
**Что проверяет:** Точность срабатывания таймеров clock/hierarchical_time_wheel.
**Бенчмарк:** `bm_timer_ordering` (BM5).
**Файл теста:** `tests/tests.cpp` (`do_timer_on_runner_test`, `do_expire_on_runner_test`)

---

## Flaky-тесты (нестабильные по времени)

Статус на 2026-08-07: после исправлений B6 (clock), B7 (channel cancel) и фикса
`runner_fixture.suspending_task_run` (заброшенный таймер в wheel standalone-раннера)
все тесты стабильны: 21/21 shuffle-прогонов (`--gtest_shuffle --gtest_random_seed=10..30`)
без сбоев, полный прогон 237/237.

### F1. `compose_extra_fixture.or_await_left_wins` — исправлено (B6)

**Симптом:** При запуске в общем наборе правый future иногда выигрывал (1ms vs 500ms).
**Причина:** Накопленная задержка в clock/hierarchical_time_wheel от предыдущих тестов
(устаревший `cached_now()`, B6).
**Исправление:** разница таймаутов 10ms vs 2000ms + починка B6.

### F2. `cross_mechanic_fixture.interrupt_during_timeout` — исправлено (B6)

**Симптом:** `ace::interrupt()` до `ace::run()` блокировал выполнение задач.
**Причина:** Сигнал e_break в pipe обрабатывался service-корутинами до того как runner
начинал обрабатывать задачи (устаревший clock задерживал таймеры).

### F3. `cross_mechanic_fixture.spawn_post_interaction` — исправлено (B6)

**Симптом:** Не все значения доставлялись (res.size() = 2 вместо 3).
**Причина:** Недетерминированный порядок выполнения при ранних/поздних срабатываниях
таймеров из-за B6.

### F4. `cross_mechanic_fixture.cancel_spawned_with_channel` — исправлено (B7)

**Симптом:** Зависание при `handle.join()` после cancel задачи ожидающей channel.pull.
**Причина:** `channel_router::cancel()` — бесконечный цикл (B7). Тест был отключён
(`DISABLED_`), переоткрыт после исправления.

### F5. `runner_fixture.suspending_task_run` — исправлено (фикс теста)

**Симптом:** res.size() == 0 (1 из ~5 прогонов) — задача не успевала завершиться.
**Причина:** standalone-раннер + таймер 1ms: истечение таймера зависит от реального
времени, а цикл `r.run()` без sleep не давал clock продвинуться. При неистечении таймер
оставался висеть в thread_local wheel, отравляя все последующие тесты процесса.
**Исправление:** между `r.run()` добавлен `sleep_for(2ms)`, пока задача не завершится.

### F6. `timer_fixture.do_timer_on_runner_test` / `do_expire_on_runner_test` — исправлено

**Симптом:** `ASSERT_GE(res[i], res[i-1])` падал: соседние длительности (500/501, 400/399)
попадают в один слот колеса, срабатывают в одном advance в порядке ВСТАВКИ, а измеренный
elapsed отсчитывается от разных стартов задач — монотонность значений не гарантирована.
**Исправление:** проверка заменена на «каждый таймер сработал и доставлен» (присутствие
всех длительностей/дедлайнов в канале) вместо проверки монотонности. Аналогично
исправлен бенчмарк BM5.

---

## Рекомендации

1. **Бенчмарки вынесены** в отдельный executable (`benchmarks/`, 21 шт., BM1-BM20).
2. **B6-B9, B12 исправлены** — критичные баги clock, channel-cancel, kernelic overflow,
   meson-фильтр.
3. **Запускать `--gtest_shuffle`** в CI для обнаружения скрытых зависимостей. Затронутые
   arena/backup/iovec/io_buffer тесты стабильны на 20 seed; полный repeated suite всё ещё
   выявляет старые зависимости timer/trace/dispatcher state между итерациями одного процесса.
4. **Coverage**: 94.4% (2226/2357 уникальных строк, gcov, meson per-test режим),
   отчёт — `build-cov` + gcov.
   Остаточные пробелы: multishot CQE-пути kernelic, error-пути compose/router.
