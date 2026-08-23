# ACE Framework — Test Coverage Plan

Цель: 95-100% покрытия кодовой базы + проверка всех механик и их взаимодействий.

> **Статус (2026-08-23):** покрытие **94.4%** (2226/2357 уникальных строк по
> `include/ace/**`, gcov + meson per-test). Отдельные модули — 85-100%.
> План ниже актуализирован под фактическое состояние тестов.

## Требования к генерации тестов (agent instructions)

1. **Приоритет исправления проекта над тестами**: если сгенерированные тесты не проходят из-за ошибок в проекте — исправлять исходный код, а не «костылить» тесты. Тесты должны проверять корректное поведение, а не маскировать баги.

2. **Приоритет использования публичного API над "костылями"**: Необходимо использовать публичные интерфейсы объектов, а не их внутренние поля, если это возможно

3. **Обязательные комментарии**:
   - Каждый `TEST_F` / `TEST` должен предваряться комментарием, описывающим **что конкретно проверяет тест**.
   - В теле теста должны быть комментарии в формате ответа на вопрос: **«Почему я это проверяю, и почему я это проверяю именно так?»**.

4. **Актуализация плана**: после выполнения работ по плану агент обязан обновить TEST_PLAN.md — отметить реализованные тесты (✅), обновить карту фикстур и индексацию.

---

## Оглавление

1. [Интеграция coverage-инструментов](#интеграция-coverage)
2. [Сводка текущего покрытия](#текущее-покрытие)
3. [План по модулям](#план-по-модулям)
   - [3.1 core/tools/*](#31-coretools)
   - [3.2 core/traits/*](#32-coretraitss)
   - [3.3 core/control.h](#33-corecontrolh)
   - [3.4 core/async.h](#34-coreasynch)
   - [3.5 core/runner.h](#35-corerunnerh)
   - [3.6 core/dispatcher.h](#36-coredispatcherh)
   - [3.7 core/signal.h](#37-coresignalh)
   - [3.8 core/compose.h](#38-corecomposeh)
   - [3.9 core/async_handle.h](#39-coreasync_handleh)
   - [3.10 io.h](#310-ioh)
   - [3.11 net.h](#311-neth)
   - [3.12 services/kernelic.h](#312-serviceskernelich)
   - [3.13 services/clock.h](#313-servicesclockh)
   - [3.14 futures/timeout.h](#314-futurestimeouth)
   - [3.15 futures/channel.h](#315-futureschannelh)
   - [3.16 futures/cutex.h](#316-futurescutexh)
   - [3.17 futures/spawn|post|reattach|roaming|polling|get_runner](#317-управление-задачами)
   - [3.18 ace_entry.cpp](#318-ace_entrycpp)
   - [3.19 fs.h](#319-fsh)
   - [3.20 console.h](#320-consoleh)
   - [3.21 futures/backup.h](#321-futuresbackuph)
   - [3.22 core/arena.h (arena_fixture)](#322-corearenah-arena_fixture)
4. [Кросс-механизмы (взаимодействия)](#кросс-механизмы)
5. [Обновление meson.build и discover_tests.py](#обновление-сборки)
6. [Карта fixture-классов (итоговая)](#карта-fixture-классов)
7. [Индексация по структуре тестов](#индексация-по-структуре-тестов)

---

## Интеграция coverage

### Шаг 1: meson_options.txt ✅
```
option('coverage', type: 'boolean', value: false, description: 'Enable gcov coverage')
```

### Шаг 2: meson.build — coverage-режим ✅
В блоке `if tests_enabled` добавлено:
```python
coverage_enabled = get_option('coverage')
if coverage_enabled
    message('Coverage mode: --coverage flags added')
    compile_args += ['--coverage', '-O0', '-g']
    coverage_link_args = ['--coverage']
else
    coverage_link_args = []
endif
```

Передаётся `link_args: coverage_link_args` в `executable(...)`.

### Шаг 3: Скрипт генерации отчёта
Добавить `scripts/coverage.sh` (опционально):
```bash
#!/bin/bash
BUILD_DIR=${1:-build}
ninja -C "$BUILD_DIR" ace_tests
./"$BUILD_DIR"/ace_tests
lcov --capture --directory "$BUILD_DIR" --output-file coverage.info --no-external
lcov --remove coverage.info '/usr/*' '*/subprojects/*' '*/tests/*' --output-file coverage_filtered.info
genhtml coverage_filtered.info --output-directory coverage_report
echo "Report: coverage_report/index.html"
```

### Шаг 4: discover_tests.py
Добавить опциональный аргумент `--list` для явного разделения list/run режимов (защита от случайного запуска тестов при discovery).

---

## Текущее покрытие

**Измерение (2026-08-23):** gcov по ACE-заголовкам, чистый режим meson per-test
(276 прогонов), уникальные строки (множественные include и template-инстанцирования
не учитываются повторно).

**Итог: 2226/2357 = 94.4%**

| Модуль | Покрыто | Примечания |
|--------|---------|-----------|
| `core/tools/queue.h` | 95.0% | slab_mempool, queue, q_node — полный набор |
| `core/tools/omniptr.h` | 100% | + lifetime 100% |
| `core/tools/id_alloc.h` | 100% | |
| `core/tools/moving_average.h` | 100% | |
| `core/traits/future.h` | 100% | compile-time |
| `core/traits/promise.h` | 97.4% | операторы new/delete покрыты |
| `core/traits/routing.h` | 89.5% | router_slot — полный набор |
| `core/traits/service.h` | 100% | |
| `core/control.h` | 100% | |
| `core/async.h` | 99.0% | |
| `core/async_handle.h` | 85.5% | join/ping handler-ы |
| `core/runner.h` | 92.0% | |
| `core/dispatcher.h` | 89.0% | |
| `core/signal.h` | 100% | |
| `core/compose.h` | 86.9% | or/and/composed, operator>> |
| `io.h` | 94.1% | query/buffer/entity/guard/hanged/any |
| `net.h` | 94.9% | TCP echo, UDP, sendmsg/recvmsg |
| `services/kernelic.h` | 91.5% | включая overflow-буфер (6000 запросов) |
| `services/clock.h` | 97.6% | включая expire |
| `futures/timeout.h` | 95.5% | |
| `futures/channel.h` | 95.3% | включая cancel и pending_push |
| `futures/cutex.h` | 92.2% | |
| `futures/backup.h` | 97.7% | callable/task payload, insure, emergency, LIFO stack |
| `futures/spawn.h` / `post.h` | 100% / 100% | |
| `futures/reattach.h` | 100% | включая кросс-раннерную миграцию |
| `futures/roaming.h` / `polling.h` / `get_runner.h` | 100% | |
| `fs.h` | 96.9% | |
| `console.h` | 100% | |
| `core/arena.h` | 95.5% | AR1-AR18; coroutine frames + I/O + framework containers |

**Не покрыто (остаточные пробелы):** multishot CQE-пути kernelic (accept-multishot не
используется в тестах), error-пути compose (несовместимые типы — compile-time),
`channel::channel_st` (e_regular режим), `kernelic` null-observer CQE.

**Как измерить:**
```bash
meson setup build-cov -Dtests=true -Dcoverage=true
ninja -C build-cov ace_tests
meson test -C build-cov
cd build-cov && ln -sfn ../tests tests && ln -sfn ../include include
gcov -b -o ace_tests.p/tests_tests.cpp.gcda ace_tests.p/tests_tests.cpp.gcno
# затем свести уникальные строки по include/ace/**/*.h (см. scripts/cov_summary.py)
```
> ⚠️ `ln -sfn ../tests tests` обязателен: gcov резолвит пути gcno относительно
> build-каталога, без симлинков показывает 0%.

---

## План по модулям

**Легенда**: ✅ — тест написан и проходит, ⬜ — тест не написан

### 3.1 core/tools/*

#### `queue.h` — `queue_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| Q1 | `slab_mempool_alloc_free` | alloc() возвращает ненулевой указатель, free() возвращает в пул, повторный alloc() переиспользует | ✅ |
| Q2 | `slab_mempool_grow` | После 1024 alloc() вызывается grow(), следующие alloc() работают | ✅ |
| Q3 | `slab_mempool_destructor` | ~slab_mempool() освобождает все слабы | ✅ |
| Q4 | `queue_enqueue_dequeue` | enqueue(T&&) + dequeue() возвращает правильное значение, empty() в процессе | ✅ |
| Q5 | `queue_enqueue_const_ref` | enqueue(const T&) работает | ✅ |
| Q6 | `queue_pop` | pop() возвращает узел, unlink без destruct | ✅ |
| Q7 | `queue_remove_node` | remove_node удаляет середину очереди, prev/next корректны | ✅ |
| Q8 | `q_node_remove` | q_node::remove() вызывает owning_queue->remove_node | ✅ |
| Q9 | `queue_move_constructor` | Перемещённая очередь работает | ✅ |
| Q10 | `queue_order` | Множественный enqueue → dequeue сохраняет FIFO порядок | ✅ |

#### `omniptr.h` — `omniptr_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| O1 | `default_construction` | omniptr по умолчанию — nullptr, operator bool = false | ✅ |
| O2 | `typed_construction` | Конструирование из T* работает, as<T>() возвращает тот же указатель | ✅ |
| O3 | `void_star_construction` | Конструирование из void* | ✅ |
| O4 | `copy_construction` | Копирование сохраняет указатель | ✅ |
| O5 | `move_construction` | Move оставляет исходный nullptr | ✅ |
| O6 | `implicit_conversion` | operator T*() неявное приведение | ✅ |
| O7 | `const_conversion` | operator const T*() const | ✅ |
| O8 | `void_star_conversion` | operator void*() и operator const void*() | ✅ |
| O9 | `arrow_operator` | operator->() даёт доступ к первому параметру | ✅ |
| O10 | `address_of_operator` | operator&() возвращает T** | ⬜ (баг const-correctness в omniptr.h) |
| O11 | `reset` | reset() обнуляет указатель | ✅ |
| O12 | `equality` | operator== с другим omniptr | ✅ |
| O13 | `wrong_type_cast` | Конструирование из типа не из списка — SFINAE (compile-time check) | ⬜ |

#### `id_alloc.h` — `id_alloc_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| I1 | `id_alloc_free_cycle` | alloc() → free() → alloc() возвращает тот же ID | ✅ |
| I2 | `id_alloc_unique` | Последовательные alloc() дают уникальные ID | ✅ |
| I3 | `id_alloc_exhaust` | Исчерпание пула (если есть лимит) | ⬜ |
| I4 | `async_id_allocator` | Синглтон + alloc/free работает | ✅ |

#### `moving_average.h` — `moving_average_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| M1 | `moving_average_basic` | add(val) → value() возвращает среднее | ✅ |
| M2 | `moving_average_zero` | value() при отсутствии данных = 0 | ✅ |
| M3 | `moving_average_window` | После 4+ значений окно скользит корректно | ✅ |
| M4 | `moving_average_stability` | Постоянное значение → среднее = значение | ✅ |
| M5 | `moving_average_clear` | clear() сбрасывает все значения | ✅ (добавлен) |
| M6 | `moving_average_copy` | Копирование сохраняет состояние | ✅ (добавлен) |
| M7 | `moving_average_move` | Move очищает источник | ✅ (добавлен) |

#### `lifetime.h` — тесты в `omniptr_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| L1 | `lifetime_track` | track() включает логирование | ✅ |
| L2 | `lifetime_mark` | mark() возвращает переданную строку | ✅ |

---

### 3.2 core/traits/*

#### `future.h` — `future_traits_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| F1 | `is_awaitable_concept` | `static_assert` на типах: task, promise<> | ✅ |
| F2 | `is_future_concept` | `static_assert` на future-типах: timeout, capture_future | ✅ |
| F3 | `is_busy_future_concept` | `static_assert` на busy future (once_suspend) | ✅ |
| F4 | `replace_type` | Замена void на monostate, замена не-void без изменений | ✅ |
| F5 | `unique_tuple` | Удаление дубликатов из tuple | ✅ |
| F6 | `tuple_to_variant` | Конвертация tuple<int,string> → variant<int,string> | ✅ |
| F7 | `at_pack` | Извлечение по индексу из parameter pack | ✅ |
| F8 | `resume_type` | deduction возвращаемого типа из awaitable | ✅ |

#### `promise.h` — `promise_traits_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| P1 | `permanent_tag_action` | `action()` = suspend_never | ✅ |
| P2 | `differed_tag_action` | `action()` = suspend_always | ✅ |
| P3 | `automaton_tag_action` | `action()` = suspend_never, без control_block | ✅ |
| P4 | `return_traits_void` | return_void(), `_return_value` отсутствует | ✅ |
| P5 | `return_traits_typed` | return_value(v), `_return_value` содержит значение | ✅ |
| P6 | `yield_value` | yield_value(v) → `_return_value` сохраняется, status = e_executed_with_value | ⬜ |
| P7 | `await_transform_future` | future-тип → `_busy_future = nullptr` | ✅ |
| P8 | `await_transform_busy` | busy-future-тип → `_busy_future = &future` | ⬜ |
| P9 | `operator_new_new` | Аллокация: control_block перед promise, frame_size корректен | ✅ |
| P10 | `operator_delete` | Деаллокация: disown вызывается, память освобождается | ⬜ |
| P11 | `operator_delete_sized` | sized delete — тот же путь что и unsized | ⬜ |
| P12 | `setup_trace` | setup_trace() возвращает уникальный возрастающий ID | ✅ |

#### `routing.h` — `router_slot_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| R1 | `router_slot_empty` | По умолчанию пуст: operator bool = false | ✅ |
| R2 | `router_slot_assign_move` | operator=(router_t&&) сохраняет router, get() != nullptr | ✅ |
| R3 | `router_slot_assign_copy` | operator=(const router_t&) копирует | ✅ |
| R4 | `router_slot_steal` | operator<< переносит из другого слота, исходный обнуляется | ✅ |
| R5 | `router_slot_release` | release() вызывает виртуальный деструктор, get() = nullptr после | ✅ |
| R6 | `router_slot_reset` | reset() обнуляет БЕЗ вызова деструктора | ✅ |
| R7 | `router_slot_release_twice` | Двойной release() — без последствий | ✅ |
| R8 | `router_slot_size_limit` | `static_assert` при превышении ACE_ROUTER_MEM_SIZE | ⬜ |
| R9 | `runner_router_handle_default_cancel` | cancel() по умолчанию — no-op | ✅ |
| R10 | `redirect_not_overridden` | redirect() по умолчанию бросает logic_error | ✅ |

#### `service.h` — `service_fixture` (не реализована)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| V1 | `service_touch_spawns` | Первый touch() создаёт инстанс и spawn-ит service | ⬜ |
| V2 | `service_detach_reattach` | После detach_set(true) → touch() делает respawn | ⬜ |
| V3 | `service_signal_break` | Сигнал e_break → service приостанавливается | ⬜ |
| V4 | `service_signal_shutdown` | Сигнал e_shutdown → service завершается | ⬜ |
| V5 | `service_signal_idle` | Сигнал e_idle → service продолжает работу | ⬜ |
| V6 | `service_inspect` | inspect() возвращает инстанс без respawn | ⬜ |
| V7 | `service_thread_local` | e_thread_local: разные потоки — разные инстансы | ⬜ |
| V8 | `service_thread_shared` | e_thread_shared: все потоки — один инстанс | ⬜ |
| V9 | `service_promise_ping` | is_service_promise: ping() возвращает promise<bool> | ⬜ |
| V10 | `service_routine_ping` | is_service_routine: ping() возвращает bool | ⬜ |

---

### 3.3 core/control.h

#### `control_block_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| CB1 | `control_block_init` | После создания: _weak_refcount=1, _strong_refcount=1, _status=e_inited | ✅ |
| CB2 | `disown_strong` | disown() декрементит _strong_refcount и _weak_refcount | ✅ |
| CB3 | `disown_last` | Последний disown() → _frame_size=0, возвращает is_untracked=true | ✅ |
| CB4 | `watch_unwatch` | watch() инкрементит _weak_refcount, unwatch() декрементит | ✅ |
| CB5 | `is_untracked` | Если оба счётчика = 0 → true | ✅ |
| CB6 | `is_disowned` | _frame_size == 0 → true | ✅ |
| CB7 | `get_block_from_address` | Корректно вычисляет адрес control_block из promise | ✅ |
| CB8 | `control_block_handle_default` | По умолчанию: _block = nullptr | ✅ |
| CB9 | `handle_cancel` | cancel() вызывает router->cancel() и ставит e_detached | ⬜ |
| CB10 | `handle_cancel_no_router` | cancel() без роутера — no-op | ✅ |
| CB11 | `handle_done` | done() → _frame_size == 0 | ✅ |
| CB12 | `handle_finished` | finished() → _status == e_finished | ✅ |
| CB13 | `handle_is_idle` | is_idle() когда handle не ссылается на блок | ✅ |
| CB14 | `handle_forward` | forward() вызывает _control_router->redirect() | ⬜ |
| CB15 | `handle_forward_null` | forward(nullptr) → false | ✅ |
| CB16 | `handle_forward_done` | forward() на завершённой корутине → false | ✅ |
| CB17 | `handle_copy` | Копирование инкрементит weak_refcount | ✅ |
| CB18 | `handle_destroy` | Деструктор декрементит weak_refcount | ✅ |

---

### 3.4 core/async.h

#### Расширение `context_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| A1 | `automaton_no_cancel_in_dtor` | automaton-корутина: нет control_block, ~async() не вызывает cancel | ✅ |
| A2 | `observe_twice` | observe() возвращает control_block_handle, повторный observe() — тот же блок | ✅ |
| A3 | `async_track` | track() возвращает trace ID | ✅ |
| A4 | `async_track_dead` | track() возвращает unexpected для мёртвой корутины | ✅ |
| A5 | `async_prefetch` | prefetch() не падает | ✅ |
| A6 | `release_waiters` | release_waiters() пробуждает зарегистрированные waiter-ы | ⬜ |
| A7 | `release_router` | release_router() освобождает текущий router | ⬜ |
| A8 | `async_destructor_with_router` | ~async() вызывает router->cancel() + release() | ⬜ |
| A9 | `async_destructor_no_router` | ~async() без роутера — только _coroutine.destroy() | ⬜ |
| A10 | `await_ready_done` | inner coroutine done → await_ready = true | ⬜ |
| A11 | `await_ready_with_router` | inner has _runner_router → await_ready = false (не будить) | ⬜ |
| A12 | `await_ready_resumable` | inner resumable → _coroutine.resume(), проверка done | ⬜ |
| A13 | `await_suspend_propagates_runner` | Внешняя корутина копирует _runner во внутреннюю | ⬜ |
| A14 | `await_suspend_steals_router` | Внешняя корутина забирает router через operator<< | ⬜ |
| A15 | `await_resume_void` | await_resume() для void-корутины | ⬜ |
| A16 | `await_resume_typed` | await_resume() для typed-корутины возвращает значение | ⬜ |
| A17 | `task_wrap_works` | task_wrap() оборачивает async<T> в task | ✅ |
| A18 | `get_current_pool` | Возвращает текущий runner pool или nullptr вне runner | ⬜ |
| A19 | `is_exist_false_when_done` | coroutine.done() → is_exist() = false | ✅ |
| A20 | `async_move_leaves_source_null` | После move: source._coroutine = nullptr | ✅ |

---

### 3.5 core/runner.h

#### `runner_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| RN1 | `reattach_same_runner` | reattach на том же раннере → push_node в _pool | ⬜ |
| RN2 | `reattach_different_runner` | reattach на другом раннере → push_node в _insert_pool | ⬜ |
| RN3 | `reattach_front_same` | reattach_front → push_node_front | ⬜ |
| RN4 | `reattach_front_different` | reattach_front на другом → _insert_pool | ⬜ |
| RN5 | `reattach_idle_context` | reattach на idle контексте → runtime_error | ⬜ |
| RN6 | `attach_increments_tasks` | attach() → ++_tasks_amount | ⬜ |
| RN7 | `attach_front_increments_tasks` | attach_front() → ++_tasks_amount | ⬜ |
| RN8 | `yank_non_resumable` | yank() задачи с e_detached → release_node, --_tasks_amount | ⬜ |
| RN9 | `yank_with_router` | yank() задачи с router → redirect(node) | ⬜ |
| RN10 | `yank_polling` | yank() задачи с _polling → service_pool | ⬜ |
| RN11 | `yank_service` | yank_service() обрабатывает service-задачи | ⬜ |
| RN12 | `fetch_task_node_local` | fetch из _pool когда _pull_source = e_local_pool | ⬜ |
| RN13 | `fetch_task_node_insert` | fetch из _insert_pool когда _pool пуст | ⬜ |
| RN14 | `fetch_task_node_empty` | Оба пула пусты → null omni_node | ⬜ |
| RN15 | `run_returns_false_when_idle` | run() когда нет задач → false | ✅ |
| RN16 | `run_processes_128` | run() обрабатывает до 128 задач | ⬜ |
| RN17 | `velocity_empty` | velocity() возвращает 0 на пустом раннере | ✅ |
| RN18 | `clear_velocity` | clear_velocity() сбрасывает счётчики | ✅ |
| RN19 | `empty_all_pools` | Все три пула пусты → empty() = true | ✅ |
| RN20 | `empty_with_tasks` | Есть задачи в любом пуле → empty() = false | ✅ |
| RN21 | `runner_move` | Move-конструктор переносит задачи | ✅ |
| RN22 | `attach_and_run` | attach(task) + run() выполняет задачу | ✅ (добавлен) |
| RN23 | `suspending_task_run` | Задача с таймаутом обрабатывается раннером | ✅ (добавлен) |

---

### 3.6 core/dispatcher.h

#### `dispatcher_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| D1 | `schedule_and_run` | schedule(task) → задача выполняется | ✅ |
| D2 | `schedule_specific_runner` | schedule(task, runner*) → задача на конкретный раннер | ⬜ |
| D3 | `reload_increase` | reload() с увеличением _runners_amount | ✅ |
| D4 | `reload_decrease` | reload() с уменьшением (лишние раннеры останавливаются) | ✅ |
| D5 | `reload_same` | reload() без изменений — без эффекта | ⬜ |
| D6 | `empty_after_run` | Все раннеры idle → empty() = true | ✅ |
| D7 | `empty_with_tasks` | Есть задачи → empty() = false | ⬜ |
| D8 | `interrupt_signal` | interrupt() посылает e_break → не падает | ✅ |
| D9 | `terminate_signal` | terminate() посылает e_shutdown → не падает | ✅ |
| D10 | `reset_signal` | reset_signal() очищает signal_pipe (вызывается в TearDown) | ⬜ |
| D11 | `round_robin_distribution` | Задачи распределяются равномерно по раннерам | ⬜ |
| D12 | `worker_round_lifecycle` | worker_round() обрабатывает задачи, спит при idle | ⬜ |
| D13 | `config_fetch` | fetch_config() читает g_config._runners_amount | ⬜ |
| D14 | `multi_runner_parallelism` | 4 раннера = задачи выполняются параллельно | ⬜ |
| D15 | `multiple_schedule_run` | Последовательные schedule+run работают | ✅ (добавлен) |

---

### 3.7 core/signal.h

#### `signal_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| S1 | `termination_signal_action` | action() → e_shutdown | ✅ |
| S2 | `interruption_signal_action` | action() → e_break | ✅ |
| S3 | `sig_pipe_push_pop` | push() → pop() возвращает тот же signal_handler | ✅ |
| S4 | `sig_pipe_empty` | Пустой pipe: pop() возвращает null | ✅ |
| S5 | `signal_in_service` | Сигнал e_shutdown останавливает service | ⬜ |
| S6 | `signal_break_in_service` | Сигнал e_break приостанавливает service | ⬜ |

---

### 3.8 core/compose.h

#### `compose_extra_fixture` (расширение compose-тестов)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| C1 | `or_await_left_wins` | or: левый завершается первым → index 0 | ✅ |
| C2 | `or_await_right_wins` | or: правый завершается первым → index 1 | ⬜ |
| C3 | `or_await_void_void` | or двух void → результат int | ⬜ |
| C4 | `or_await_typed_void` | or(T, void) → optional<T> | ⬜ |
| C5 | `or_await_void_typed` | or(void, T) → optional<T> | ⬜ |
| C6 | `or_await_typed_typed` | or(T, U) → variant<T, U> | ⬜ |
| C7 | `and_await_both_succeed` | and: оба завершаются → результат void | ✅ |
| C8 | `and_await_typed_void` | and(T, void) → T | ⬜ |
| C9 | `and_await_void_typed` | and(void, T) → T | ⬜ |
| C10 | `and_await_typed_typed` | and(T, U) → tuple<T, U> | ⬜ |
| C11 | `operator_pipe` | pipe: цепочка выполняется | ✅ |
| C12 | `operator_pipe_void` | void >> void: вызов без аргументов | ⬜ |
| C13 | `compose_function` | compose(sender, responder) | ⬜ |
| C14 | `or_await_composed_3` | or(a, b, c) — три future | ✅ (исправлен баг variant emplace в or_await::observer для typed-race) |
| C15 | `and_await_composed_3` | and(a, b, c) — три future | ⬜ |
| C16 | `or_await_cancel_observer` | cancel одного из observer-ов в or | ⬜ |
| C17 | `or_await_router_cancel` | cancel во время гонки | ⬜ |
| C18 | `or_await_two_typed` | or(T, T) — одинаковые типы: проверка что emplace вместо get работает | ✅ (фикс: или в or_ping_automaton_loop_no_value_loss) |

---

### 3.9 core/async_handle.h

#### `spawn_extra_fixture` (расширение spawn-тестов)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| AH1 | `join_handler_await_ready` | await_ready = false пока не done | ⬜ |
| AH2 | `join_handler_await_ready_done` | await_ready = true когда done | ⬜ |
| AH3 | `join_handler_await_suspend` | await_suspend регистрирует waiter | ⬜ |
| AH4 | `join_handler_await_resume` | await_resume возвращает finished (false для void-корутин — баг?) | ⬜ |
| AH5 | `spawn_and_join` | spawn → done-опрос → задача завершена | ✅ |
| AH6 | `join_after_cancel` | join() на отменённой → false | ✅ |
| AH7 | `handle_done` | done() возвращает true после завершения | ✅ |
| AH8 | `handle_cancel` | cancel() отменяет корутину | ⬜ |
| AH9 | `check_valued_spawn_cancel` | join() на отменённой valued-таске → nullopt (cancel не даёт статусу стать e_finished) | ✅ |
| AH10 | `check_valued_spawn_join_value` | join() на завершённой valued-таске → возвращает правильное значение из co_return | ✅ |

---

### 3.10 io.h

#### `io_buffer_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| B1 | `buffer_expand` | expand(len) выделяет память, возвращает ненулевой указатель | ✅ |
| B2 | `buffer_expand_multiple` | Несколько expand() — корректный суммарный len() | ✅ |
| B3 | `buffer_append_format` | append(fmt, args...) добавляет форматированную строку | ✅ |
| B4 | `buffer_append_and_as_string` | append(string_view) → as<string>() | ✅ |
| B5 | `buffer_append_raw` | append(void*, void*) — копирование байтового диапазона | ✅ |
| B6 | `buffer_append_vector` | append(vector<int>) — копирование POD вектора | ✅ |
| B7 | `buffer_append_array` | append(array<int,N>) — копирование POD массива | ✅ |
| B8 | `buffer_append_span_fixed` | append(span<int,N>) — копирование POD span с фиксированным размером | ✅ |
| B9 | `buffer_prepend_format` | prepend(fmt, args...) добавляет форматированную строку в начало | ✅ |
| B10 | `buffer_prepend_string_view` | prepend(string_view) добавляет в начало | ✅ |
| B11 | `buffer_appendln` | appendln добавляет строку + \n | ✅ |
| B12 | `buffer_assemble` | assemble() → msghdr* с правильным iov_len и данными | ✅ |
| B13 | `buffer_assemble_once` | Повторный assemble() — возвращает тот же msghdr* (effect-once guard) | ✅ |
| B14 | `buffer_disassemble` | disassemble() сбрасывает msg_iov, позволяет reassemble | ✅ |
| B15 | `buffer_shape` | shape(len) обрезает хвостовой чанк до len | ✅ |
| B16 | `buffer_shape_single` | shape на единственном чанке → замена чанка меньшего размера | ✅ |
| B17 | `buffer_clone` | clone() создаёт копию с теми же данными | ✅ |
| B18 | `buffer_clear` | clear() освобождает все чанки, len() = 0 | ✅ |
| B19 | `buffer_move` | Move-конструктор переносит данные, исходный пуст | ✅ |
| B20 | `buffer_as_string` | as<string>() собирает все чанки в строку | ✅ |
| B21 | `buffer_as_bytes` | as<vector<byte>>() возвращает побайтовое представление | ✅ |
| B22 | `buffer_len` | len() возвращает суммарную длину | ✅ (через expand_multiple) |
| B23 | `buffer_formatter` | std::format("{}", buf) работает | ✅ |
| B24 | `buffer_move_assign` | Move-присваивание: данные переносятся, источник очищается | ✅ |
| B25 | `buffer_prepend_raw` | prepend(void*, void*) — prepend байтового диапазона | ✅ |
| B26 | `buffer_append_span_dynamic` | append(span<int>) — копирование POD span с dynamic_extent | ✅ |
| B27 | `buffer_expand_overflow` | `len + control_hdr_len` проверяется до allocation, buffer остаётся пустым | ✅ |

#### `IoQueryFixture` (не реализована)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| IQ1-IQ12 | Все тесты io::query | ⬜ |

#### `io_any_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| AY1 | `any_default_construction` | any по умолчанию: не падает | ✅ |
| AY2 | `any_construct_int` | any(int) — placement new + корректный deleter | ✅ |
| AY3 | `any_construct_string` | any(string) — placement new для non-trivial типа | ✅ |
| AY4 | `any_release` | release() освобождает память через deleter | ✅ |
| AY5 | `any_move` | Move-конструктор с exchange обнуляет источник | ✅ |
| AY6 | `any_destructor` | Деструктор с данными не падает | ✅ |
| AY7 | `any_copy` | Копирование запрещено (shallow-copy → double-free) | ⬜ (удалён) |

#### `io_hanged_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| IH1 | `hanged_basic_fail_handler` | basic_fail_handler с отрицательным res бросает runtime_error | ✅ |
| IH2 | `hanged_fail_handler_positive` | basic_fail_handler с неотрицательным res — no-op | ✅ |
| IH3 | `hanged_command_pool_exists` | command_pool thread_local доступен | ✅ |
| IH4 | `hanged_command_pool_capture` | capture() из пула возвращает команду | ✅ |
| IH5 | `hanged_command_defaults` | command по умолчанию: buffer пуст, user_data пуст | ✅ |

#### `io_entity_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| IE1 | `entity_default_construction` | entity по умолчанию: _fd = -1, _is_closed = true | ✅ |
| IE2 | `entity_param_construction` | entity(fd, false) сохраняет параметры | ✅ |
| IE3 | `entity_move` | Move-конструктор переносит FD, источник инвалидируется | ✅ |
| IE4 | `entity_extract` | extract() возвращает FD и инвалидирует сущность | ✅ |
| IE5 | `entity_close` | close() помечает _is_closed и возвращает close_query | ✅ |
| IE6 | `entity_is_closed` | is_closed() возвращает корректный статус | ✅ |
| IE7 | `entity_guard_no_runner` | guard с невалидным FD не падает | ✅ |
| IE8 | `guard_valid_fd_no_runner` | guard с валидным FD без раннера: schedule pending_close | ✅ |
| IE9 | `guard_already_closed` | guard с _closed=true не закрывает FD повторно | ✅ |

#### `IoHangedFixture`, `IoAnyFixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| IH1-IH5, AY1-AY7 | Все тесты io::hanged + io::any | ✅ |

---

### 3.11 net.h

#### `socket_echo_fixture` + `udp_fixture` (не расширена)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| N1-N35 | Все тесты net.h | ⬜ (существующие: do_io_socket_echo, do_io_socket_echo_zc) |

---

### 3.12 services/kernelic.h

#### `KernelicFixture` (не реализована)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| KC1-KC16 | Все тесты | ⬜ |

---

### 3.13 services/clock.h

#### `ClockFixture` (не реализована), частично покрыто через `timer_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| CL1-CL18 | Все тесты clock | ⬜ (timeout тесты покрывают базовый сценарий) |

---

### 3.14 futures/timeout.h

#### Расширение `timer_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| T1 | `timeout_zero` | timeout(0ms) → почти мгновенное завершение | ✅ |
| T2 | `timeout_negative` | Отрицательный timeout → поведение | ⬜ |
| T3 | `expire_past` | expire(время в прошлом) → мгновенное завершение | ⬜ |
| T4 | `expire_future` | expire(время в будущем) → задержка | ⬜ |
| T5 | `timeout_cancel_before_clock` | cancel до того как clock обработал | ⬜ |
| T6 | `timeout_cancel_after_clock` | cancel после того как таймер истёк | ⬜ |
| T7 | `timeout_router_cancel_reattach` | cancel() возвращает ноду в runner | ⬜ |
| T8 | `timeout_multiple_concurrent` | 20 одновременных таймеров → все завершаются | ✅ |
| T9 | `timeout_short` | timeout(10ms) → допустимая погрешность | ✅ (добавлен) |

---

### 3.15 futures/channel.h

#### `channel_extra_fixture` (расширение)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| CH1 | `push_pull_single` | push → pull возвращает то же значение | ✅ |
| CH2 | `push_full` | push на полный канал → false | ⬜ |
| CH3 | `pending_push` | pending_push ждёт освобождения | ⬜ |
| CH4 | `pending_push_rvalue` | pending_push(T&&) | ⬜ |
| CH5 | `operator_left_shift` | ch << val → push | ✅ |
| CH6 | `channel_empty` | empty() на пустом/непустом | ✅ |
| CH7 | `pull_suspends` | pull на пустом канале → корутина суспендится | ⬜ |
| CH8 | `mpsc_channel` | Несколько producer-ов, один consumer | ✅ |
| CH9 | `channel_mpmc` | Несколько producer-ов и consumer-ов | ⬜ |
| CH10 | `channel_st` | channel_st (single-thread) вариант | ⬜ |
| CH11 | `channel_bounded` | bounded шина: push блокируется при заполнении | ⬜ |
| CH12 | `channel_dyn` | dyn шина: динамическое расширение | ⬜ |
| CH13 | `channel_router_cancel` | cancel во время ожидания pull | ⬜ |
| CH14 | `channel_notify` | notify пробуждает ожидающих | ⬜ |
| CH15 | `channel_router_redirect` | redirect сохраняет waiter | ⬜ |
| CH16 | `channel_sp_sc` | SPSC режим | ⬜ |

---

### 3.16 futures/cutex.h

#### `cutex_extra_fixture` (расширение)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| CX1 | `try_lock_free` | try_lock() на свободном → true | ✅ |
| CX2 | `cutex_try_lock_busy` | try_lock() на занятом → false | ⬜ |
| CX3 | `cutex_capture_suspends` | capture() на занятом → корутина суспендится | ⬜ |
| CX4 | `cutex_sync_wakes` | sync() пробуждает следующего ожидающего | ⬜ |
| CX5 | `proxy_double_capture` | Повторный capture() без sync() → logic_error | ✅ |
| CX6 | `proxy_double_sync` | Повторный sync() — no-op | ✅ |
| CX7 | `proxy_destructor_sync` | ~proxy() вызывает sync() автоматически | ✅ |
| CX8 | `cutex_proxy_volatile` | volatile proxy корректно работает | ⬜ |
| CX9 | `cutex_notify_rescheduling` | _rescheduling = true → waiter мигрирует | ⬜ |
| CX10 | `cutex_pending_notify` | pending_notify: если notify() не удался → повтор | ⬜ |
| CX11 | `cutex_cancel_in_queue` | cancel задачи в очереди ожидания | ⬜ |
| CX12 | `cutex_cancel_after_capture` | cancel после захвата → proxy деструктор освобождает | ⬜ |
| CX13 | `cutex_guard_raii` | ace::guard + co_await capture → авто sync | ⬜ |
| CX14 | `cutex_high_contention` | 100+ корутин на одном cutex → порядок FIFO | ⬜ |
| CX15 | `cutex_notify_no_waiter` | notify() без ожидающих → false | ⬜ |
| CX16 | `cutex_router_redirect` | redirect сохраняет waiter в очередь | ⬜ |
| CX17 | `set_rescheduling` | set_rescheduling/get_rescheduling работают | ✅ (добавлен) |

---

### 3.17 Управление задачами

#### `spawn_extra_fixture` (расширение)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| SP1 | `spawn_returns_handle` | co_await spawn(task) → async_handle | ✅ |
| SP2 | `spawn_on_specific_runner` | spawn с указанием runner | ⬜ |
| SP3 | `post_uses_attach_front` | post → задача в начало очереди | ✅ |
| SP4 | `reattach_changes_runner` | reattach(runner*) → _runner обновляется | ⬜ |
| SP5 | `reattach_router_redirect` | reattach_router::redirect обновляет _runner + reattach | ⬜ |
| SP6 | `roaming_true` | roaming(true) → _roaming = true | ✅ |
| SP7 | `roaming_false` | roaming(false) → _roaming = false | ✅ |
| SP8 | `polling_true` | polling(true) → задача идёт в _service_pool | ✅ |
| SP9 | `get_runner_inside_runner` | get_runner внутри runner → не-nullptr | ✅ |
| SP10 | `check_valued_spawn_command` | spawn valued-таски (async<int>), join → возвращает значение, spawner и spawnee на одном раннере | ✅ |
| SP11 | `check_valued_post_command` | post valued-тасок + and-композиция (4 таски) → правильный порядок значений, join возвращает std::optional<int> | ✅ |
| SP12 | `check_valued_spawn_cancel` | spawn valued-таски (async<int>), cancel до завершения → join возвращает std::nullopt (статус не e_finished) | ✅ |
| SP13 | `check_valued_spawn_join_value` | spawn быстрой valued-таски (co_return 42), join → возвращает std::optional<int> с правильным значением | ✅ |

### carrier — обёртка для valued-тасок в раннере

`runner::carrier(async<T>*)` (`runner.h:206`) — замена `task_wrap()` для valued-тасок. В отличие от `task_wrap` (один `co_await` с воровством router), `carrier` в цикле вызывает `inner.await_ready()`, ворует router внутренней корутины через `carrier_suspend`, и ждёт пока внутренняя корутина полностью завершится. Это позволяет обрабатывать несколько suspension point'ов.

`carrier_suspend` (`runner.h:221`) — awaitable, который при `await_suspend` переносит router внутренней корутины на promise обёртки (`operator<<`), позволяя `yank()` обработать router на correct-ноде.

---

### 3.18 ace_entry.cpp

#### `entry_fixture` (не реализована — требует отдельной сборки)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| E1-E3 | Все тесты | ⬜ |

---

### 3.19 fs.h

#### `fs_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| FS1 | `file_open_rdonly` | open_rdonly() → file_link | ⬜ |
| FS2 | `file_open_wronly` | open_wronly() → file_link | ⬜ |
| FS3 | `file_open_rewrite` | open_rewrite() → file_link | ⬜ |
| FS4 | `file_open_fail` | open() несуществующего файла на чтение → error | ✅ |
| FS5 | `file_write` | write() через file_link | ✅ (в file_write_and_read) |
| FS6 | `file_writeln` | writeln() через file_link | ✅ (в file_write_and_read) |
| FS7 | `file_read` | read() через file_link | ✅ (в file_write_and_read) |
| FS8 | `file_read_str` | read_str() через file_link | ⬜ |
| FS9 | `file_output_action` | output_action использует kernel_controller::writev | ⬜ |
| FS10 | `file_input_action` | input_action использует read_query | ⬜ |
| FS11 | `file_write_and_read` | Полный цикл write → read → verify | ✅ (добавлен) |

---

### 3.20 console.h

#### `console_fixture`

Тесты вывода идут через **короткие алиасы** `ace::println` / `ace::print`
(свободные функции в `ace`, определены в `console.h` под `#ifdef ACE_H` —
доступны, т.к. `tests/environment.h` включает `ace/ace.h` раньше `ace/console.h`).
`ace::println("{}", 42)` ≡ `ace::console::println("{}", 42)` — покрывается тот
же код консоли. Алиас `ace::input` (≡ `ace::console::input`) в тестах не
используется (stdin не покрыт).

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| CN1 | `println_format` | println(fmt, args...) | ⬜ |
| CN2 | `println_string_view` | println(string_view) через `ace::println` | ✅ |
| CN3 | `println_empty` | println() — пустая строка | ✅ |
| CN4 | `print_format` | print(fmt, args...) без newline через `ace::print` | ✅ |
| CN5 | `print_string_view` | print(string_view) | ✅ |
| CN6 | `stdin_link` | stdin_link() возвращает валидный link | ⬜ |
| CN7 | `stdout_link` | stdout_link() возвращает валидный link | ⬜ |
| CN8 | `input_link` | input_link() для stdin | ⬜ |
| CN9 | `output_link` | output_link() для stdout | ⬜ |

---

### 3.21 futures/backup.h — backup / insure / emergency (добавлен)

#### `backup_fixture` (добавлен)

Механика страховки: `co_await backup(callable|task)` регистрирует коллбек,
выполняемый при отмене корутины (не дошла до `co_return`) в обратном порядке
(LIFO); `co_await insure(...)` — одноразовая страховка на следующую
co_await/co_yield операцию (снимается при её успешном прохождении, сбрасывается
новой регистрацией); `co_await emergency(bool)` — флаг срабатывания на
необработанных исключениях (дефолт из `ace::cfg::g_config._emergency_default`).

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| BK1 | `backup_cancel_fires_lifo` | 3 backup + cancel → выполнение в обратном порядке [3,2,1] | ✅ |
| BK2 | `backup_normal_completion_no_fire` | `co_return` → коллбеки не выполнены | ✅ |
| BK3 | `backup_destroy_incomplete_fires` | eager promise в main (без runner): ~async() → fire через `ace::schedule` fallback | ✅ |
| BK4 | `backup_fire_scheduled_not_inline` | fire НЕ инлайновый: маркер драйвера (100) до коллбека (1) в канале | ✅ |
| BK5 | `backup_task_payload_awaited` | task-коллбек co_await-ится до завершения (с его таймером), LIFO-смесь callable/task | ✅ |
| BK6 | `insure_fires_when_cancelled_during_protected_await` | отмена на защищаемой co_await → страховка сработала | ✅ |
| BK7 | `insure_dropped_after_passing_protected_await` | защищаемая co_await пройдена → страховка снята | ✅ |
| BK8 | `insure_dropped_when_next_op_ready` | следующая операция готова синхронно (roaming) → страховка снята (механизм `_insured_prev`) | ✅ |
| BK9 | `insure_replaced_by_backup` | регистрация backup вытесняет insure → при отмене только backup | ✅ |
| BK10 | `insure_replaced_by_insure` | регистрация insure вытесняет insure → при отмене только новый | ✅ |
| BK11 | `insure_automaton_yield_fires` | automaton: отмена на co_yield → страховка сработала | ✅ |
| BK12 | `insure_automaton_yield_dropped` | automaton: co_yield пройден (resume) → страховка снята | ✅ |
| BK13 | `insure_loop_bounded` | цикл 9x{insure+co_await} → при отмене срабатывает только последняя страховка (bounded-память) | ✅ |
| BK14 | `emergency_default_exception_fires` | исключение, дефолт true → коллбеки сработали (путь ~async) | ✅ |
| BK15 | `emergency_true_exception_fires` | явный emergency(true) + исключение → сработали | ✅ |
| BK16 | `emergency_false_exception_no_fire` | emergency(false) + исключение → не сработали | ✅ |
| BK17 | `emergency_config_default` | `g_config._emergency_default = false` → новые корутины не срабатывают на исключениях | ✅ |
| BK18 | `backup_in_automaton_cancel` | automaton: backup + cancel на co_yield → сработал | ✅ |
| BK19 | `backup_cancel_via_spawn_handle` | spawn + `async_handle::cancel` → backup сработал, join=false | ✅ |
| BK20 | `backup_stack_many_records_fires_lifo` | 64 arena-backed list nodes → callbacks выполняются строго в обратном порядке | ✅ |

> ⚠️ **Pre-existing баг фреймворка (не связан с этой фичей):** `observe()` на
> лямбда-корутине перед `schedule()`/spawn портит захваченные ссылки — GCC
> размещает closure лямбды в кадре так, что он накладывается на поле `_block`
> promise, и `setup_control_block()` затирает захват. Поэтому тесты отмены
> используют helper-функции (не лямбды) и отмену изнутри раннера
> (spawn + async_handle). Баг воспроизводится на чистом HEAD.

---

### 3.22 core/arena.h — arena_fixture

`ace::core::arena` — единая thread-local арена для coroutine frames, I/O/iovec и
framework containers. Чанки ≤ 4096 обслуживаются `std::pmr::unsynchronized_pool_resource`,
большие — transient-malloc. `allocate_as<T>(count)` поддерживает оба пути и проверяет
переполнение. `arena_allocator<T>` подключает arena к стандартным контейнерам; backup
использует `std::stack` поверх `std::list<backup_record, arena_allocator<backup_record>>`.
Заголовок чанка хранит владельца, поэтому pooled foreign-free возвращается через MPSC
канал, а transient foreign-free освобождается сразу с отложенной коррекцией accounting.
Лимит `_max_allocation_size` общий для всех клиентов arena.

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| AR1 | `small_alloc_served_from_pool` | малая аллокация, выравнивание, accounting и retention пула | ✅ |
| AR2 | `big_alloc_goes_to_malloc` | большая аллокация использует transient и сразу освобождается | ✅ |
| AR3 | `chunk_reuse_after_free` | повторная аллокация переиспользует pooled chunk | ✅ |
| AR4 | `pool_never_returns_to_system` | освобождённые pooled chunks удерживаются до деструктора arena | ✅ |
| AR5 | `arena_is_thread_local` | у каждого треда свой singleton arena | ✅ |
| AR6 | `cross_thread_free_returns_to_owner` | foreign pooled-free возвращается владельцу через канал | ✅ |
| AR7 | `channel_drain_cadence` | формула `N=max/occupied` управляет drain cadence | ✅ |
| AR8 | `limit_zero_drains_every_alloc` | max=0 дренирует канал перед каждой аллокацией | ✅ |
| AR9 | `occupied_zero_drains_every_alloc` | occupied=0 не делит на ноль и дренирует канал | ✅ |
| AR10 | `limit_breach_fallback_malloc` | общий лимит: transient fallback или `bad_alloc` | ✅ |
| AR11 | `destructor_returns_everything` | выход треда освобождает pool/channel/transient storage | ✅ |
| AR12 | `promise_traits_uses_arena` | coroutine frame учитывается общей arena | ✅ |
| AR13 | `foreign_transient_free_updates_owner_accounting` | foreign transient-free корректирует owner accounting | ✅ |
| AR14 | `typed_allocation_uses_arena` | typed small/large arrays используют pool/transient пути | ✅ |
| AR15 | `typed_allocation_overflow` | переполнение `count*sizeof(T)` даёт `bad_array_new_length` | ✅ |
| AR16 | `arena_allocator_list_storage` | `std::list` nodes выделяются и освобождаются через arena | ✅ |
| AR17 | `iovec_uses_shared_arena` | kernel iovec API меняет статистику того же singleton | ✅ |
| AR18 | `cross_thread_iovec_release` | iovec storage безопасно возвращается owner arena с другого треда | ✅ |

> 📝 Замечания по реализации:
> - `pop_batch()` из nukes оказался нерабочим для вычитывания (итератор стартует с
>   dummy-ноды очереди — первый `*it` читает мусор), поэтому канал дренируется обычным
>   `pop()` в цикле.
> - Счётчик операций инкрементируется только в ветке формулы утилизации (ветки
>   «max==0» и «occupied==0» счётчик не двигают) — нумерация операций не совпадает с
>   фактическим числом операций.
> - `live_system_chunks`/`pool_held_bytes` считают и служебные upstream-аллокации пула
>   libstdc++ (528B при конструировании, 192B на класс) — тесты не контрактят их точные
>   значения, только относительные дельты.
> - `extern_release` выбирается через `std::conditional_t`: debug-вариант содержит
>   атомарный `malloc_count`, release-вариант — только канал и `released_bytes`.

---

## Кросс-механизмы

Тесты взаимодействия нескольких подсистем одновременно.

> ⚠️ Ниже — исходный список плана. **Все пункты кроме X4, X5, X6, X7, X8, X10,
> X14, X15, X16, X18, X22, X23 реализованы и проходят.** Дополнительно реализованы
> тесты X26-X33 (см. конец раздела).

#### `cross_mechanic_fixture`

| # | Тест | Взаимодействие | Статус |
|---|------|---------------|--------|
| X1 | `cancel_spawned_with_timeout` | spawn → timeout → cancel: проверка что cancel освобождает таймер и корутину | ✅ |
| X2 | `cancel_spawned_with_channel` | spawn → channel.pull → cancel: waiter удаляется из канала | ✅ |
| X3 | `cancel_spawned_with_cutex` | spawn → cutex.capture → cancel: proxy освобождает cutex | ⬜ |
| X4 | `cancel_spawned_with_recv` | spawn → recv → cancel: io_uring запрос отменяется | ⬜ |
| X5 | `reattach_during_timeout` | timeout → reattach на другой раннер → таймер срабатывает на новом раннере | ⬜ |
| X6 | `roaming_with_spawn_and_cutex` | roaming + spawn на другом раннере + cutex: гонка с миграцией | ⬜ |
| X7 | `polling_with_timeout` | polling(true) → timeout → задача в service + clock | ⬜ |
| X8 | `or_compose_with_cancel` | timeout or recv → cancel ор-композиции → оба observer-а отменяются | ⬜ |
| X9 | `and_compose_with_cancel` | spawn and channel → cancel → оба observer-а отменяются | ✅ |
| X10 | `pipe_with_channel` | pusher >> channel.pull: значение передаётся через канал | ⬜ |
| X11 | `spawn_post_interaction` | spawn + post одновременно → порядок выполнения | ✅ |
| X12 | `channel_with_timeout` | timeout or timeout → гонка | ✅ |
| X13 | `cutex_with_timeout` | cutex.capture or timeout → гонка за мьютексом | ✅ |
| X14 | `cutex_with_channel` | Под cutex пишем в channel | ⬜ |
| X15 | `reattach_with_cancel` | reattach во время выполнения → cancel на новом раннере | ⬜ |
| X16 | `dispatcher_reload_during_run` | reload() во время run() → раннеры переконфигурируются | ⬜ |
| X17 | `multi_runner_cutex_count` | 4 раннера, 4×1000 корутин на cutex → счётчик корректен | ✅ |
| X18 | `multi_runner_channel` | 4 раннера, producer/consumer через канал | ⬜ |
| X19 | `multi_runner_spawn` | spawn на разных раннерах, все завершаются | ✅ |
| X20 | `interrupt_during_timeout` | interrupt() во время timeout → корутина завершается | ✅ |
| X21 | `terminate_during_run` | terminate() во время run → все раннеры останавливаются | ✅ |
| X22 | `socket_echo_multi_client` | 2+ клиентов на одном сервере | ⬜ |
| X23 | `socket_echo_with_cancel` | recv or timeout → cancel выигравшей ветки | ⬜ |
| X24 | `fs_write_read_cycle` | write → read → verify | ✅ |
| X25 | `stress_spawn_cancel` | 100 spawn → cancel всех → нет утечек | ✅ |
| X26 | `channel_clean_after_run` | Каналы пусты после run (no waiter leak) | ✅ (добавлен) |
| X27 | `or_ping_automaton_loop_no_value_loss` | 2 automaton → or-гонка ping в цикле (8 итераций) → проверка что cancel_yield не разрушает автоматон и не теряет co_yield значения | ✅ (добавлен) |
| X28 | `channel_clean_after_run` | Каналы пусты после run (no waiter leak) | ✅ |
| X29 | `kernelic_overflow_buffer_stress` | 6000 висящих read (>4096 ring) → overflow-буфер kernel_entity → все завершаются | ✅ (добавлен, покрывает B8/B9) |
| X30 | `reattach_nullptr_noop` | reattach(nullptr) → await_ready=true, задача не суспендится | ✅ (добавлен) |
| X31 | `reattach_cross_runner_migration` | 2 раннера: задача мигрирует между ними через reattach_router::redirect | ✅ (добавлен) |
| X32 | `cancel_spawned_with_channel` (переоткрыт) | spawn → channel.pull → cancel — БЫЛ DISABLED (hang, B7); после фикса channel_router::cancel проходит стабильно | ✅ (переоткрыт) |
| X33 | `do_timer_on_runner_test` / `do_expire_on_runner_test` (переработаны) | Проверка «каждый таймер сработал» вместо неверной монотонности (F6) | ✅ (переработаны) |

---

## Обновление сборки

### meson_options.txt ✅
Добавлена опция `coverage`.

### meson.build ✅
В блоке `if tests_enabled` добавлены coverage-флаги, передача `link_args` в `executable(...)`.

### discover_tests.py
Исправить: добавить поддержку флага `--list-only` чтобы list-режим не запускал тесты полного прохода. Если передан `--list-only` — выполнить `--gtest_list_tests`. Иначе — обычный запуск с `--gtest_filter`.

### scripts/coverage.sh
Создать скрипт (опционально, см. выше).

---

## Карта fixture-классов (итоговая)

| Fixture | Наследует | Статус | Тестов (план/факт) |
|---------|----------|--------|---------------------|
| `base_fixture` | `::testing::Test` | ✅ существующий + расширен | —/17 (io/kernelic/udp/tcp/reattach) |
| `context_fixture` | `base_fixture` | ✅ | 5→13 (✅ 13) |
| `channel_fixture` | `base_fixture` | ✅ | 1→1 |
| `timer_fixture` | `base_fixture` | ✅ | 5→10 (✅ 10) |
| `yield_fixture` | `base_fixture` | ✅ **добавлен** (automaton) | —→7 (✅ 7) |
| `cutex_fixture` | `base_fixture` | ✅ | 4→4 |
| `spawn_fixture` | `base_fixture` | ✅ | 6→10 (✅ 10) |
| `socket_echo_fixture` | `base_fixture` | ✅ | 2→2 |
| `fs_fixture` | `base_fixture` | ✅ | 1→4 (✅ 4) |
| `queue_fixture` | `::testing::Test` | ✅ | 10→10 (✅ 10) |
| `omniptr_fixture` | `::testing::Test` | ✅ (+lifetime 2) | 13→12 (✅ 12) |
| `id_alloc_fixture` | `::testing::Test` | ✅ | 4→3 (✅ 3) |
| `moving_average_fixture` | `::testing::Test` | ✅ | 4→7 (✅ 7) |
| `future_traits_fixture` | `::testing::Test` | ✅ | 8→8 (✅ 8) |
| `promise_traits_fixture` | `base_fixture` | ✅ | 12→8 (✅ 8) |
| `router_slot_fixture` | `::testing::Test` | ✅ | 10→8 (✅ 8) |
| `signal_fixture` | `base_fixture` | ✅ | 6→4 (✅ 4) |
| `control_block_fixture` | `::testing::Test` | ✅ | 18→15 (✅ 15) |
| `runner_fixture` | `base_fixture` | ✅ | 21→10 (✅ 10, suspending_task_run починен) |
| `dispatcher_fixture` | `base_fixture` | ✅ | 14→8 (✅ 8) |
| `io_buffer_fixture` | `::testing::Test` | ✅ | 27→25 (✅ 25) |
| `io_entity_fixture` | `::testing::Test` | ✅ | 9→9 (✅ 9) |
| `io_hanged_fixture` | `::testing::Test` | ✅ | 5→5 (✅ 5) |
| `io_any_fixture` | `::testing::Test` | ✅ | 6→6 (✅ 6) |
| `console_fixture` | `::testing::Test` | ✅ | 9→4 (✅ 4) |
| `cross_mechanic_fixture` | `base_fixture` | ✅ | 25→17 (✅ 17, включая переоткрытый cancel_spawned_with_channel) |
| `spawn_extra_fixture` | `base_fixture` | ✅ | —→12 (✅ 12) |
| `compose_extra_fixture` | `base_fixture` | ✅ | —→3 (✅ 3) |
| `channel_extra_fixture` | `base_fixture` | ✅ | —→4 (✅ 4) |
| `cutex_extra_fixture` | `base_fixture` | ✅ | —→5 (✅ 5) |
| `get_runner_fixture` | `base_fixture` | ✅ | —→1 (✅ 1) |
| `backup_fixture` | `base_fixture` | ✅ **добавлен** (backup/insure/emergency) | —→20 (✅ 20) |
| `arena_fixture` | `::testing::Test` | ✅ (shared arena) | —→18 (✅ 18) |

**Итого:** 34 fixture-класса, **276 тестов** (269 существующих + 7 новых);
в meson-режиме 276 зарегистрированных прогонов, все активные.

> Примечание: `timer_parallel_fixture` и `service_fixture`/`io_query_fixture`/
> `kernelic_fixture`/`clock_fixture`/`entry_fixture` из ранней версии плана не
> реализованы как отдельные fixture-классы — их сценарии покрыты внутри
> `timer_fixture` (parallel) и `base_fixture` (io_query/kernelic/clock/udp/tcp).

**Бенчмарки:** 21 бенчмарк в `benchmarks/` (BM1-BM20, см. `BUGS_AND_BENCHMARKS.md`).
Запуск: `meson setup build-bench -Dbenchmarks=true && ninja -C build-bench ace_benchmarks`

---

## Индексация по структуре тестов

### Файлы тестовой инфраструктуры

| Файл | Назначение |
|------|-----------|
| `tests/main.cpp` | GTest entry point |
| `tests/units.h` | Include-хаб: подключает все заголовки + `environment.h` + `fixtures.h` |
| `tests/environment.h` | Все fixture-классы и вспомогательные корутины (helpers) |
| `tests/tests.cpp` | Все тесты (`TEST_F` / `TEST`) |

### Расположение fixture-классов в `tests/environment.h`

| Строки (примерно) | Fixture | Категория |
|-------------------|---------|-----------|
| 27–72 | `base_fixture` | Базовый |
| 78–95 | `context_fixture` | async |
| 97–123 | `channel_fixture` | channel |
| 125–192 | `timer_fixture` | timeout |
| 194–306 | `yield_fixture` | automaton |
| 308–420 | `cutex_fixture` | cutex |
| 422–642 | `spawn_fixture` | spawn/post/compose |
| 644–723 | `socket_echo_fixture` | net |
| 725–736 | `fs_fixture` | fs |
| 738–750 | `queue_fixture` | tools |
| 752–756 | `omniptr_fixture` | tools |
| 758–762 | `id_alloc_fixture` | tools |
| 764–768 | `moving_average_fixture` | tools |
| 770–774 | `future_traits_fixture` | traits |
| 776–788 | `promise_traits_fixture` | traits |
| 790–816 | `router_slot_fixture` | traits |
| 818–822 | `signal_fixture` | signal |
| 824–871 | `control_block_fixture` | control |
| 873–890 | `runner_fixture` | runner |
| 892–907 | `dispatcher_fixture` | dispatcher |
| 909–913 | `io_buffer_fixture` | io |
| 915–924 | `io_entity_fixture` | io |
| 926–930 | `io_any_fixture` | io |
| 932–936 | `io_hanged_fixture` | io |
| 938–942 | `console_fixture` | console |
| 944–1004 | `cross_mechanic_fixture` | integration |
| 1006–1023 | `spawn_extra_fixture` | futures |
| 1025–1047 | `compose_extra_fixture` | compose |
| 1049–1060 | `channel_extra_fixture` | channel |
| 1062–1083 | `cutex_extra_fixture` | cutex |
| 1085–1093 | `get_runner_fixture` | futures |
| 1082–1140 | `backup_fixture` | futures (backup/insure/emergency) |
| 1175–1211 | `arena_fixture` | core (arena) |

### Расположение тестов в `tests/tests.cpp`

| Строки | Fixture | Количество тестов |
|--------|---------|-------------------|
| 9–48 | `context_fixture` | 5 (базовые) |
| 49–74 | `timer_fixture` | 3 (or/and/or_with_promise) |
| 76–163 | `yield_fixture` | 7 (automaton) |
| 164–182 | `fs_fixture` + `channel_fixture` | 2 |
| 183–208 | `timer_fixture` | 1 (do_timer_on_runner_test) |
| 209–234 | `timer_fixture` | 1 (do_expire_on_runner_test) |
| 236–258 | `cutex_fixture` | 2 (race) |
| 259–297 | `timer_fixture` | 1 (parallel) |
| 298–315 | `socket_echo_fixture` | 2 |
| 316–441 | `spawn_fixture` | 10 |
| 441–502 | `cutex_fixture` | 2 (cancel) |
| 503–672 | `queue_fixture` | 10 |
| 673–969 | `omniptr_fixture` (+2 lifetime) | 12 |
| 970–989 | `id_alloc_fixture` | 3 |
| 990–1061 | `moving_average_fixture` | 7 |
| 1062–1168 | `future_traits_fixture` | 8 |
| 1169–1283 | `promise_traits_fixture` | 8 |
| 1284–1380 | `router_slot_fixture` | 8 |
| 1381–1563 | `control_block_fixture` | 15 |
| 1564–1634 | `signal_fixture` | 4 |
| 1635–1718 | `runner_fixture` | 8 (suspending_task_run починен) |
| 1719–1821 | `dispatcher_fixture` | 7 |
| 1822–2128 | `io_buffer_fixture` | 25 |
| 2118–2231 | `io_entity_fixture` | 9 |
| 2232–2272 | `io_any_fixture` | 6 |
| 2273–2316 | `io_hanged_fixture` | 5 |
| 2317–2348 | `console_fixture` | 4 |
| 2349–2444 | `context_fixture` (async ext) | 8 |
| 2445–2565 | `spawn_extra_fixture` | 8 |
| 2566–2627 | `compose_extra_fixture` | 3 |
| 2628–2715 | `channel_extra_fixture` | 4 |
| 2716–2828 | `cutex_extra_fixture` | 5 |
| 2829–2831 | `get_runner_fixture` | 1 |
| 2832–3240 | `cross_mechanic_fixture` | 17 (включая переоткрытый cancel_spawned_with_channel) |
| 3241–3320 | `timer_fixture` (timeout ext) | 3 |
| 3321–3400 | `fs_fixture` (fs ext) | 3 |
| 3401–3470 | `base_fixture` (kernelic: nop, pipe r/w, close, iovec, register_files, overflow) | 6 |
| 3471–3650 | `base_fixture` (channel bounded/pending/spsc/mpmc, get_current_pool, router, reattach×3, udp, tcp) | 14 |
| 3749–4237 | `backup_fixture` | 20 |
| 4240–4800 | `arena_fixture` | 18 |

> ⚠️ Номера строк приблизительные и сдвигаются при правках. Источник истины —
> `grep -n '^TEST' tests/tests.cpp`.

---
