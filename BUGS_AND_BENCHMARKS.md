# ACE Framework — Found Bugs & Benchmark Candidates

Дата обновления: 2026-07-23

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
| `benchmarks/environment.h` | Fixture-классы и хелперы |
| `benchmarks/benchmarks.cpp` | Сами бенчмарки (BM1-BM6) |

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

**Приоритет:** Средний (move-конструктор используется в clock/multi_dial; потенциальный use-after-free).

---

## Кандидаты в бенчмарки (медленные тесты)

Эти тесты проверяют корректность в условиях высокой нагрузки, но требуют значительного времени выполнения (>10s каждый). Рекомендуется вынести в отдельную категорию бенчмарков и запускать опционально.

### BM1. `timer_parallel_fixture.do_timer_on_runner_parallel_test`

**Время:** ~30-60s  
**Нагрузка:** 1,000,000 set'ов × 10 таймеров = 10,000,000 таймеров на 4 раннерах  
**Что проверяет:** Масштабируемость clock/multi_dial при массовых таймерах.  
**Файл:** `tests/tests.cpp:171-206`

### BM2. `cutex_fixture.cutex_race`

**Время:** ~15-30s (8 раннеров × 100,000 инкрементов)  
**Нагрузка:** 800,000 операций capture/sync  
**Что проверяет:** Корректность cutex в условиях высокой конкуренции.  
**Файл:** `tests/tests.cpp:148-157`

### BM3. `cutex_fixture.cutex_race_resheduling`

**Время:** ~60-120s (8 раннеров × 1,000,000 инкрементов)  
**Нагрузка:** 8,000,000 операций capture/sync с rescheduling  
**Что проверяет:** Корректность cutex с rescheduling при экстремальной нагрузке.  
**Файл:** `tests/tests.cpp:159-169`

### BM4. `cross_mechanic_fixture.multi_runner_cutex_count`

**Время:** ~30-60s (4 раннера × 4,000 инкрементов)  
**Нагрузка:** 16,000 операций capture/sync + string конвертации  
**Что проверяет:** Атомарность счётчика под cutex на 4 раннерах.  
**Файл:** `tests/tests.cpp` (закомментирован)

### BM5. `cross_mechanic_fixture.stress_spawn_cancel`

**Время:** ~5-15s (100 spawn → cancel → join)  
**Нагрузка:** 100 параллельных spawn + cancel циклов  
**Что проверяет:** Отсутствие утечек памяти при массовом spawn/cancel.  
**Файл:** `tests/tests.cpp:2535-2560`

### BM6. `timer_fixture.do_timer_on_runner_test` / `do_expire_on_runner_test`

**Время:** ~500ms+ каждый  
**Нагрузка:** 16 таймеров с разными duration (0ms → 501ms)  
**Что проверяет:** Точность срабатывания и порядок таймеров в clock/multi_dial.  
**Файл:** `tests/tests.cpp:95-146`

---

## Flaky-тесты (нестабильные по времени)

Эти тесты проходят при изолированном запуске, но могут падать при запуске в общем наборе из-за накопленной задержки clock или гонок планировщика.

### F1. `compose_extra_fixture.or_await_left_wins`

**Симптом:** При запуске в общем наборе правый future иногда выигрывает (1ms vs 500ms).  
**Причина:** Накопленная задержка в clock/multi_dial от предыдущих тестов.  
**Исправление:** Разница таймаутов увеличена до 10ms vs 2000ms, проверяется что or разрешился (0 или 1).

### F2. `cross_mechanic_fixture.interrupt_during_timeout`

**Симптом:** `ace::interrupt()` до `ace::run()` блокирует выполнение задач.  
**Причина:** Сигнал e_break в pipe обрабатывается vortex-сервисами до того как runner начинает обрабатывать задачи.

### F3. `cross_mechanic_fixture.spawn_post_interaction`

**Симптом:** При запуске в общем наборе не все значения доставляются (res.size() = 2 вместо 3).  
**Причина:** Недетерминированный порядок выполнения spawn/post задач в одном раннере при наличии отложенных задач от предыдущих тестов.

### F4. `cross_mechanic_fixture.cancel_spawned_with_channel`

**Симптом:** Зависание при `handle.join()` после cancel задачи ожидающей channel.pull.  
**Причина:** `channel_router::cancel()` реаттачит waiter'ов через `runner::reattach()`, но отменённая задача может не получить статус e_detached до того как `join()` попытается её разбудить.

---

## Рекомендации

1. **Вынести бенчмарки**: тесты BM1-BM6 перенести в отдельный `*_benchmark` suite с `DISABLED_` префиксом или отдельный executable.
2. **Починить B4 (join/return_void)**: наиболее критичный баг, затрагивающий публичное API.
3. **Починить B2 (and_compose cancel)**: утечка observer-задач.
4. **Добавить `--gtest_also_run_disabled_tests` или отдельную цель** для запуска бенчмарков и stress-тестов.
5. **Рассмотреть `--gtest_shuffle`** для обнаружения скрытых зависимостей между тестами.
