# ACE Framework — Test Coverage Plan

Цель: 95-100% покрытия кодовой базы + проверка всех механик и их взаимодействий.

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
4. [Кросс-механизмы (взаимодействия)](#кросс-механизмы)
5. [Обновление meson.build и discover_tests.py](#обновление-сборки)
6. [Карта fixture-классов (итоговая)](#карта-fixture-классов)

---

## Интеграция coverage

### Шаг 1: meson_options.txt
Добавить опцию:
```
option('coverage', type: 'boolean', value: false, description: 'Enable gcov coverage')
```

### Шаг 2: meson.build — coverage-режим
В блоке `if tests_enabled` добавить:
```python
coverage_enabled = get_option('coverage')
if coverage_enabled
    message('Coverage enabled: --coverage flags added')
    compile_args += ['--coverage', '-O0', '-g']
    link_args = ['--coverage']
else
    link_args = []
endif
```

Передавать `link_args` в `executable(..., link_args: link_args, ...)`.

### Шаг 3: Скрипт генерации отчёта
Добавить `scripts/coverage.sh`:
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

| Модуль | Покрыто | Не покрыто |
|--------|---------|------------|
| `core/tools/queue.h` | 10% | slab_mempool grow/free, q_node::remove, queue::pop/unlink/remove_node, move ctor |
| `core/tools/omniptr.h` | 70% | operator&, reset(), operator==, copy/move assignment edge cases |
| `core/tools/id_alloc.h` | 0% | весь модуль |
| `core/tools/moving_average.h` | 80% | краевые случаи (переполнение, нулевое окно) |
| `core/tools/lifetime.h` | 50% | untrack(), mark() |
| `core/traits/future.h` | 60% | concepts, type traits (unique_tuple, replace_type, tuple_to_variant) |
| `core/traits/promise.h` | 70% | automaton tag, return_traits краевые случаи, operator delete(size_t) |
| `core/traits/routing.h` | 60% | router_slot::operator= копирование, release() с деструктором, reset() |
| `core/traits/vortex.h` | 50% | detach_set/detach_get, respawn, vortex() цикл с сигналами, inspect() |
| `core/control.h` | 40% | disown, watch/unwatch, is_untracked, is_disowned, get_block_from_address, forward edge cases |
| `core/async.h` | 60% | ~async c router, release_router, prefetch, track, async_router::redirect, task_wrap, get_current_pool, automaton paths, observe edge cases |
| `core/runner.h` | 60% | reattach_front, attach_front, yank_vortex, run цикл >128 задач, velocity, clear_velocity, upgrade_velocity, empty |
| `core/dispatcher.h` | 50% | worker_round, round_robin, fetch_config, reload с уменьшением, interrupt/terminate сигналы, многопоточное взаимодействие |
| `core/signal.h` | 0% | весь модуль |
| `core/compose.h` | 40% | or_await_composed, and_await_composed, compose(), observer(), все router-ы |
| `core/async_handle.h` | 50% | join_handler::await_ready/suspend/resume, join_handler_router, done |
| `io.h` | 30% | io::buffer (все методы!), io::guard RAII close, io::hanged, io::any, query_router::cancel, read_query/write_query/close_query standalone |
| `net.h` | 20% | UDP (sendto/recv), connected UDP, sendmsg/recvmsg, все bind/connect/accept overloads, error-пути, connection_link, io::caster |
| `services/kernelic.h` | 30% | cancel/cancel_fd/nop, multishot, overflow buffer (>4096), ring params, iovec allocator |
| `services/clock.h` | 30% | multi_dial уровни 2+, detach_record реаттач, dial::migrate, time_slot::release_slot(limit) |
| `futures/timeout.h` | 60% | expire краевые случаи, timeout нулевой, cancel на разных стадиях |
| `futures/channel.h` | 40% | pending_push, channel_st, allocation_type/access_mode варианты, channel_router::cancel |
| `futures/cutex.h` | 50% | notify с rescheduling, pending_notify цикл, try_lock гонка, proxy double capture exception, proxy volatile |
| `futures/spawn.h` | 50% | внутренняя механика await_suspend/await_resume |
| `futures/post.h` | 30% | внутренняя механика, attach_front взаимодействие |
| `futures/reattach.h` | 30% | reattach_router::redirect, await_ready |
| `futures/roaming.h` | 30% | roaming флаг + spawn/post взаимодействие |
| `futures/polling.h` | 30% | polling флаг + vortex взаимодействие |
| `futures/get_runner.h` | 50% | await_suspend/await_resume |
| `ace_entry.cpp` | 0% | весь модуль (если собран) |
| `fs.h` | 30% | open_query краевые случаи, file_link input_action/read |
| `console.h` | 30% | stdin input, stdout write/writeln все перегрузки |

---

## План по модулям

### 3.1 core/tools/*

#### `queue.h` — `QueueFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| Q1 | `slab_mempool_alloc_free` | alloc() возвращает ненулевой указатель, free() возвращает в пул, повторный alloc() переиспользует |
| Q2 | `slab_mempool_grow` | После 1024 alloc() вызывается grow(), следующие alloc() работают |
| Q3 | `slab_mempool_destructor` | ~slab_mempool() освобождает все слабы |
| Q4 | `queue_enqueue_dequeue` | enqueue(T&&) + dequeue() возвращает правильное значение, empty() в процессе |
| Q5 | `queue_enqueue_const_ref` | enqueue(const T&) работает |
| Q6 | `queue_pop` | pop() возвращает узел, unlink без destruct |
| Q7 | `queue_remove_node` | remove_node удаляет середину очереди, prev/next корректны |
| Q8 | `q_node_remove` | q_node::remove() вызывает owning_queue->remove_node |
| Q9 | `queue_move_constructor` | Перемещённая очередь работает, исходная пуста |
| Q10 | `queue_order` | Множественный enqueue → dequeue сохраняет FIFO порядок |

#### `omniptr.h` — `OmniptrFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| O1 | `default_construction` | omniptr по умолчанию — nullptr, operator bool = false |
| O2 | `typed_construction` | Конструирование из T* работает, as<T>() возвращает тот же указатель |
| O3 | `void_star_construction` | Конструирование из void* |
| O4 | `copy_construction` | Копирование сохраняет указатель |
| O5 | `move_construction` | Move оставляет исходный nullptr |
| O6 | `implicit_conversion` | operator T*() неявное приведение |
| O7 | `const_conversion` | operator const T*() const |
| O8 | `void_star_conversion` | operator void*() и operator const void*() |
| O9 | `arrow_operator` | operator->() даёт доступ к первому параметру |
| O10 | `address_of_operator` | operator&() возвращает T** |
| O11 | `reset` | reset() обнуляет указатель |
| O12 | `equality` | operator== с T* и с другим omniptr |
| O13 | `wrong_type_cast` | Конструирование из типа не из списка — SFINAE (compile-time check) |

#### `id_alloc.h` — `IdAllocFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| I1 | `id_alloc_free_cycle` | alloc() → free() → alloc() возвращает тот же ID |
| I2 | `id_alloc_unique` | Последовательные alloc() дают уникальные ID |
| I3 | `id_alloc_exhaust` | Исчерпание пула (если есть лимит) |
| I4 | `async_id_allocator` | Потокобезопасность (concurrent alloc/free) |

#### `moving_average.h` — `MovingAverageFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| M1 | `moving_average_basic` | add(val) → value() возвращает среднее |
| M2 | `moving_average_zero` | value() при отсутствии данных = 0 |
| M3 | `moving_average_window` | После 4+ значений окно скользит корректно |
| M4 | `moving_average_stability` | Постоянное значение → среднее = значение |

#### `lifetime.h` — добавить в `BaseFixture`

| # | Тест | Что проверяет |
|---|------|--------------|
| L1 | `lifetime_track` | track() включает логирование, untrack() выключает |
| L2 | `lifetime_mark` | mark() возвращает переданную строку |

---

### 3.2 core/traits/*

#### `future.h` — `FutureTraitsFixture` (новая фикстура, compile-time + runtime)

| # | Тест | Что проверяет |
|---|------|--------------|
| F1 | `is_awaitable_concept` | `static_assert` на типах: async<int>, task, promise<>, timeout, recv_query |
| F2 | `is_future_concept` | `static_assert` на future-типах |
| F3 | `is_busy_future_concept` | `static_assert` на busy future (once_suspend, channel pull_impl) |
| F4 | `replace_type` | Замена void на monostate, замена не-void без изменений |
| F5 | `unique_tuple` | Удаление дубликатов из tuple |
| F6 | `tuple_to_variant` | Конвертация tuple<int,string> → variant<int,string> |
| F7 | `at_pack` | Извлечение по индексу из parameter pack |
| F8 | `resume_type` | deduction возвращаемого типа из awaitable |

#### `promise.h` — `PromiseTraitsFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| P1 | `permanent_tag` | `action()` = suspend_never |
| P2 | `differed_tag` | `action()` = suspend_always |
| P3 | `automaton_tag` | `action()` = suspend_never, без control_block — корутина запускается и завершается |
| P4 | `return_traits_void` | return_void(), `_return_value` отсутствует |
| P5 | `return_traits_typed` | return_value(v), `_return_value` содержит значение |
| P6 | `yield_value` | yield_value(v) → `_return_value` сохраняется, status = e_executed_with_value |
| P7 | `await_transform_future` | future-тип → `_busy_future = nullptr` |
| P8 | `await_transform_busy` | busy-future-тип → `_busy_future = &future` |
| P9 | `operator_new` | Аллокация: control_block перед promise, frame_size корректен |
| P10 | `operator_delete` | Деаллокация: disown вызывается, память освобождается |
| P11 | `operator_delete_sized` | sized delete — тот же путь что и unsized |
| P12 | `setup_trace` | setup_trace() возвращает уникальный возрастающий ID |

#### `routing.h` — `RouterSlotFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| R1 | `router_slot_empty` | По умолчанию пуст: operator bool = false |
| R2 | `router_slot_assign_move` | operator=(router_t&&) сохраняет router, get() != nullptr |
| R3 | `router_slot_assign_copy` | operator=(const router_t&) копирует |
| R4 | `router_slot_steal` | operator<< переносит из другого слота, исходный обнуляется |
| R5 | `router_slot_release` | release() вызывает виртуальный деструктор, get() = nullptr после |
| R6 | `router_slot_reset` | reset() обнуляет БЕЗ вызова деструктора |
| R7 | `router_slot_release_twice` | Двойной release() — без последствий |
| R8 | `router_slot_size_limit` | `static_assert` при превышении ACE_ROUTER_MEM_SIZE |
| R9 | `runner_router_handle_default_cancel` | cancel() по умолчанию — no-op |
| R10 | `redirect_not_overridden` | redirect() по умолчанию бросает logic_error |

#### `vortex.h` — `VortexFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| V1 | `vortex_touch_spawns` | Первый touch() создаёт инстанс и spawn-ит vortex |
| V2 | `vortex_detach_reattach` | После detach_set(true) → touch() делает respawn |
| V3 | `vortex_signal_break` | Сигнал e_break → vortex приостанавливается |
| V4 | `vortex_signal_shutdown` | Сигнал e_shutdown → vortex завершается |
| V5 | `vortex_signal_idle` | Сигнал e_idle → vortex продолжает работу |
| V6 | `vortex_inspect` | inspect() возвращает инстанс без respawn |
| V7 | `vortex_thread_local` | e_thread_local: разные потоки — разные инстансы |
| V8 | `vortex_thread_shared` | e_thread_shared: все потоки — один инстанс |
| V9 | `vortex_promise_ping` | is_vortex_promise: ping() возвращает promise<bool> |
| V10 | `vortex_routine_ping` | is_vortex_routine: ping() возвращает bool |

---

### 3.3 core/control.h

#### `ControlBlockFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| CB1 | `control_block_init` | После operator new: _weak_refcount=0, _strong_refcount=1, _frame_size>0, _status=e_inited |
| CB2 | `disown_strong` | disown() декрементит _strong_refcount |
| CB3 | `disown_last` | Последний disown() → _frame_size=0, возвращает true |
| CB4 | `watch_unwatch` | watch() инкрементит _weak_refcount, unwatch() декрементит |
| CB5 | `is_untracked` | Если оба счётчика = 0 → true |
| CB6 | `is_disowned` | _frame_size == 0 → true |
| CB7 | `get_block_from_address` | Корректно вычисляет адрес control_block из promise |
| CB8 | `control_block_handle_default` | По умолчанию: _block = nullptr |
| CB9 | `handle_cancel` | cancel() вызывает router->cancel() и ставит e_detached |
| CB10 | `handle_cancel_no_router` | cancel() без роутера → только e_detached |
| CB11 | `handle_done` | done() → _frame_size == 0 |
| CB12 | `handle_finished` | finished() → _status == e_finished |
| CB13 | `handle_is_idle` | is_idle() когда корутина не выполняется |
| CB14 | `handle_forward` | forward() вызывает _control_router->redirect() |
| CB15 | `handle_forward_null` | forward(nullptr) → false |
| CB16 | `handle_forward_done` | forward() на завершённой корутине → false |
| CB17 | `handle_copy` | Копирование инкрементит weak_refcount |
| CB18 | `handle_destroy` | Деструктор декрементит weak_refcount, освобождает если нужно |

---

### 3.4 core/async.h

#### Расширение `ContextFixture`

| # | Тест | Что проверяет |
|---|------|--------------|
| A1 | `automaton_coroutine` | automaton-корутина: нет control_block, ~async() не вызывает cancel |
| A2 | `async_observe` | observe() возвращает control_block_handle, повторный observe() — тот же блок |
| A3 | `async_track` | track() возвращает trace ID |
| A4 | `async_prefetch` | prefetch() не падает |
| A5 | `async_release_waiters` | release_waiters() пробуждает зарегистрированные waiter-ы |
| A6 | `async_release_router` | release_router() освобождает текущий router |
| A7 | `async_destructor_with_router` | ~async() вызывает router->cancel() + release() |
| A8 | `async_destructor_no_router` | ~async() без роутера — только _coroutine.destroy() |
| A9 | `await_ready_done` | inner coroutine done → await_ready = true |
| A10 | `await_ready_with_router` | inner has _runner_router → await_ready = false (не будить) |
| A11 | `await_ready_resumable` | inner resumable → _coroutine.resume(), проверка done |
| A12 | `await_suspend_propagates_runner` | Внешняя корутина копирует _runner во внутреннюю |
| A13 | `await_suspend_steals_router` | Внешняя корутина забирает router через operator<< |
| A14 | `await_resume_void` | await_resume() для void-корутины |
| A15 | `await_resume_typed` | await_resume() для typed-корутины возвращает значение |
| A16 | `task_wrap` | task_wrap() оборачивает async<T> в task |
| A17 | `get_current_pool` | Возвращает текущий runner pool или nullptr вне runner |
| A18 | `is_exist_false_when_done` | coroutine.done() → is_exist() = false |
| A19 | `is_exist_false_when_disowned` | control_block disowned → is_exist() = false |
| A20 | `async_move_leaves_source_null` | После move: source._coroutine = nullptr, target.is_exist() |

---

### 3.5 core/runner.h

#### `RunnerFixture` (новая фикстура, т.к. нужен явный runner)

| # | Тест | Что проверяет |
|---|------|--------------|
| RN1 | `reattach_same_runner` | reattach на том же раннере → push_node в _pool |
| RN2 | `reattach_different_runner` | reattach на другом раннере → push_node в _insert_pool |
| RN3 | `reattach_front_same` | reattach_front → push_node_front |
| RN4 | `reattach_front_different` | reattach_front на другом → _insert_pool |
| RN5 | `reattach_idle_context` | reattach на idle контексте → runtime_error |
| RN6 | `attach_increments_tasks` | attach() → ++_tasks_amount |
| RN7 | `attach_front_increments_tasks` | attach_front() → ++_tasks_amount |
| RN8 | `yank_non_resumable` | yank() задачи с e_detached → release_node, --_tasks_amount |
| RN9 | `yank_with_router` | yank() задачи с router → redirect(node) |
| RN10 | `yank_polling` | yank() задачи с _polling → vortex_pool |
| RN11 | `yank_vortex` | yank_vortex() обрабатывает vortex-задачи |
| RN12 | `fetch_task_node_local` | fetch из _pool когда _pull_source = e_local_pool |
| RN13 | `fetch_task_node_insert` | fetch из _insert_pool когда _pool пуст |
| RN14 | `fetch_task_node_empty` | Оба пула пусты → null omni_node |
| RN15 | `run_returns_false_when_idle` | run() когда нет задач → false |
| RN16 | `run_processes_128` | run() обрабатывает до 128 задач |
| RN17 | `velocity` | velocity() возвращает скользящее среднее |
| RN18 | `clear_velocity` | clear_velocity() сбрасывает счётчики |
| RN19 | `empty_all_pools` | Все три пула пусты → empty() = true |
| RN20 | `empty_with_tasks` | Есть задачи в любом пуле → empty() = false |
| RN21 | `runner_move` | Move-конструктор и оператор переносят все поля |

---

### 3.6 core/dispatcher.h

#### `DispatcherFixture` (новая фикстура, многопоточная)

| # | Тест | Что проверяет |
|---|------|--------------|
| D1 | `schedule_default` | schedule(task) → задача идёт в round_robin |
| D2 | `schedule_specific_runner` | schedule(task, runner*) → задача на конкретный раннер |
| D3 | `reload_increase` | reload() с увеличением _runners_amount |
| D4 | `reload_decrease` | reload() с уменьшением (лишние раннеры останавливаются) |
| D5 | `reload_same` | reload() без изменений — без эффекта |
| D6 | `empty_all_idle` | Все раннеры idle → empty() = true |
| D7 | `empty_with_tasks` | Есть задачи → empty() = false |
| D8 | `interrupt_signal` | interrupt() посылает e_break всем раннерам |
| D9 | `terminate_signal` | terminate() посылает e_shutdown, run() завершается |
| D10 | `reset_signal` | reset_signal() сливает signal_pipe |
| D11 | `round_robin_distribution` | Задачи распределяются равномерно по раннерам |
| D12 | `worker_round_lifecycle` | worker_round() обрабатывает задачи, спит при idle |
| D13 | `config_fetch` | fetch_config() читает g_config._runners_amount |
| D14 | `multi_runner_parallelism` | 4 раннера = задачи выполняются параллельно |

---

### 3.7 core/signal.h

#### `SignalFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| S1 | `termination_signal_action` | action() → e_shutdown |
| S2 | `interruption_signal_action` | action() → e_break |
| S3 | `sig_pipe_push_pop` | push() → pop() возвращает тот же signal_handler |
| S4 | `sig_pipe_empty` | Пустой pipe: pop() возвращает null |
| S5 | `signal_in_vortex` | Сигнал e_shutdown останавливает vortex |
| S6 | `signal_break_in_vortex` | Сигнал e_break приостанавливает vortex |

---

### 3.8 core/compose.h

#### Расширение `TimerFixture` / `SpawnFixture`

| # | Тест | Что проверяет |
|---|------|--------------|
| C1 | `or_await_left_wins` | or: левый завершается первым → index 0 |
| C2 | `or_await_right_wins` | or: правый завершается первым → index 1 |
| C3 | `or_await_void_void` | or двух void → результат int |
| C4 | `or_await_typed_void` | or(T, void) → optional<T> |
| C5 | `or_await_void_typed` | or(void, T) → optional<T> |
| C6 | `or_await_typed_typed` | or(T, U) → variant<T, U> |
| C7 | `and_await_both_succeed` | and: оба завершаются → результат tuple/void |
| C8 | `and_await_typed_void` | and(T, void) → T |
| C9 | `and_await_void_typed` | and(void, T) → T |
| C10 | `and_await_typed_typed` | and(T, U) → tuple<T, U> |
| C11 | `operator_pipe` | fetch >> process: значение передаётся |
| C12 | `operator_pipe_void` | void >> void: вызов без аргументов |
| C13 | `compose_function` | compose(sender, responder) |
| C14 | `or_await_composed_3` | or(a, b, c) — три future |
| C15 | `and_await_composed_3` | and(a, b, c) — три future |
| C16 | `or_await_cancel_observer` | cancel одного из observer-ов в or |
| C17 | `or_await_router_cancel` | cancel во время гонки |

---

### 3.9 core/async_handle.h

#### Расширение `SpawnFixture`

| # | Тест | Что проверяет |
|---|------|--------------|
| AH1 | `join_handler_await_ready` | await_ready = false пока не done |
| AH2 | `join_handler_await_ready_done` | await_ready = true когда done |
| AH3 | `join_handler_await_suspend` | await_suspend регистрирует waiter |
| AH4 | `join_handler_await_resume` | await_resume возвращает done |
| AH5 | `async_handle_join_true` | join() на завершённой → true |
| AH6 | `async_handle_join_false` | join() на отменённой → false |
| AH7 | `async_handle_done` | done() возвращает статус |
| AH8 | `async_handle_cancel` | cancel() отменяет корутину |

---

### 3.10 io.h

#### `IoBufferFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| B1 | `buffer_expand` | expand(len) выделяет память, возвращает ненулевой указатель |
| B2 | `buffer_expand_multiple` | Несколько expand() — корректный iovec список |
| B3 | `buffer_append_format` | append(fmt, args...) добавляет форматированную строку |
| B4 | `buffer_append_string_view` | append(string_view) |
| B5 | `buffer_append_raw` | append(void*, void*) |
| B6 | `buffer_append_vector` | append(vector<T>) |
| B7 | `buffer_append_array` | append(array<T,N>) |
| B8 | `buffer_append_span` | append(span<T>) |
| B9 | `buffer_prepend_format` | prepend(fmt, args...) добавляет в начало |
| B10 | `buffer_prepend_string_view` | prepend(string_view) |
| B11 | `buffer_appendln` | appendln добавляет строку + \n |
| B12 | `buffer_assemble` | assemble() → msghdr* с правильным iov_len |
| B13 | `buffer_assemble_once` | Повторный assemble() — возвращает тот же msghdr* |
| B14 | `buffer_disassemble` | disassemble() сбрасывает msg_iov |
| B15 | `buffer_shape` | shape(len) обрезает хвост до len |
| B16 | `buffer_shape_single` | shape на единственном чанке → замена чанка |
| B17 | `buffer_clone` | clone() создаёт копию с теми же данными |
| B18 | `buffer_clear` | clear() освобождает все чанки, len() = 0 |
| B19 | `buffer_move` | Move-конструктор/оператор переносит данные, исходный пуст |
| B20 | `buffer_as_string` | as<string>() собирает все чанки в строку |
| B21 | `buffer_as_bytes` | as<vector<byte>>() |
| B22 | `buffer_len` | len() возвращает суммарную длину |
| B23 | `buffer_formatter` | std::format("{}", buf) работает |

#### `IoQueryFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| IQ1 | `query_await_ready` | await_ready() всегда false |
| IQ2 | `query_await_suspend_success` | setup_query=true → router установлен, возвращает true |
| IQ3 | `query_await_suspend_silent` | _is_silent=true → возвращает false (не суспендит) |
| IQ4 | `query_await_suspend_invalid_fd` | _fd < 0 → logic_error |
| IQ5 | `query_await_suspend_idle_fd` | _fd == INT_MIN → logic_error |
| IQ6 | `query_on_result` | on_result(res) → _res = res, waiter реаттачится |
| IQ7 | `query_on_result_no_waiter` | on_result без waiter → только _res = res |
| IQ8 | `query_router_redirect` | redirect(node) → _query->_waiter = node |
| IQ9 | `query_router_cancel` | cancel() → kernel_controller::cancel() |
| IQ10 | `read_query_await_resume` | Нуль-терминация буфера при _res > 0 |
| IQ11 | `write_query_await_resume` | Возвращает _res |
| IQ12 | `close_query_await_resume` | Возвращает _res |

#### `IoEntityFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| IE1 | `entity_consume` | consume() извлекает FD, создаёт новый entity |
| IE2 | `entity_extract` | extract() → tuple{fd, is_closed}, исходный недействителен |
| IE3 | `entity_move` | Move-конструктор переносит FD |
| IE4 | `entity_close` | close() → close_query, _is_closed = true |
| IE5 | `entity_operator_bool` | Валидный FD → true, -1 → false |
| IE6 | `entity_is_closed` | is_closed() отражает состояние |
| IE7 | `entity_guard_destructor` | io::guard закрывает FD в деструкторе (hanged command) |
| IE8 | `entity_guard_already_closed` | guard на закрытом FD — без эффекта |
| IE9 | `entity_guard_no_runner` | guard без раннера → schedule(pending_close) |

#### `IoHangedFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| IH1 | `hanged_command_pool` | capture() → command, raw_sync() возвращает в пул |
| IH2 | `hanged_command_on_result_error` | on_result(<0) → fail_cb_handler вызывается |
| IH3 | `hanged_command_on_result_success` | on_result(>=0) → raw_sync |
| IH4 | `hanged_fail_handler_default` | basic_fail_handler бросает runtime_error |
| IH5 | `hanged_fail_handler_custom` | Замена fail_cb_handler → вызывается кастомный |

#### `IoAnyFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| IA1 | `any_store_int` | any(42) хранит int, release() очищает |
| IA2 | `any_store_string` | any(string) хранит string |
| IA3 | `any_move` | Move-конструктор переносит данные |
| IA4 | `any_destructor` | ~any() вызывает deleter |

---

### 3.11 net.h

#### Расширение `SocketEchoFixture` + `UdpFixture` (новая)

| # | Тест | Что проверяет |
|---|------|--------------|
| N1 | `socket_tcp_creation` | socket_tcp → co_await → валидный socket_entity |
| N2 | `socket_udp_creation` | socket_udp → co_await → валидный socket_entity |
| N3 | `socket_raw_creation` | socket_raw → co_await |
| N4 | `bind_ipv4_addr` | bind(in_addr_t, port) → stream_mode_entity |
| N5 | `bind_string_view` | bind("127.0.0.1", port) → stream_mode_entity |
| N6 | `bind_udp` | bind() для UDP → net_interface (e_indirect) |
| N7 | `bind_error` | bind на занятый порт → error |
| N8 | `listen_default_backlog` | listen() → listener_entity |
| N9 | `listen_custom_backlog` | listen(42) → listener_entity |
| N10 | `accept_basic` | accept() → connection |
| N11 | `accept_filtered` | accept(addr, port) фильтрует по адресу |
| N12 | `accept_timeout` | accept с таймаутом через or |
| N13 | `connect_basic` | connect(host, port) → connection |
| N14 | `connect_error` | connect на недоступный хост → error |
| N15 | `send_string` | send(string_view) → количество байт |
| N16 | `send_raw` | send(void*, size) |
| N17 | `send_vector` | send(vector<T>) |
| N18 | `send_array` | send(array<T,N>) |
| N19 | `send_buffer` | send(io::buffer&) → sendmsg_query |
| N20 | `recv_string` | recv(string&) → количество байт |
| N21 | `recv_raw` | recv(void*, size) |
| N22 | `recv_vector` | recv(vector<T>&) |
| N23 | `recv_array` | recv(array<T,N>&) |
| N24 | `recv_buffer` | recv(io::buffer&) → recvmsg_query |
| N25 | `recv_buf` | recv_buf() → promise<expected<buffer,int>> |
| N26 | `recv_buf_large` | recv_buf с данными > 64 байт (мульти-чанк) |
| N27 | `sendto_udp` | sendto на UDP сокете |
| N28 | `recv_udp` | recv на UDP (не-connected) |
| N29 | `connected_udp` | connect на UDP → connection |
| N30 | `entity_error_handling` | operator bool(), error() на невалидном entity |
| N31 | `connection_link_write` | writeln/read через connection_link |
| N32 | `connection_link_input_action` | input_action → ::recv |
| N33 | `sendmsg_query` | sendmsg_query через io_uring |
| N34 | `recvmsg_query` | recvmsg_query через io_uring |
| N35 | `consume_chain` | Полный consume-цикл: socket → bind → listen → accept |

---

### 3.12 services/kernelic.h

#### `KernelicFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| KC1 | `kernel_controller_init` | Конструктор инициализирует io_uring ring |
| KC2 | `ping_empty` | ping() с пустым ring → false |
| KC3 | `ping_with_cqe` | ping() обрабатывает CQE → вызывает on_result |
| KC4 | `ping_multishot` | Мультишот CQE: IORING_CQE_F_MORE → observer вызывается повторно |
| KC5 | `ping_cancel` | cancel observer → CQE с _on_cancel |
| KC6 | `submit_overflow` | Более 4096 запросов → буферизация в _submission_buffer |
| KC7 | `submit_overflow_full` | Буфер переполнен → submit возвращает false |
| KC8 | `nop` | nop(observer) → observer получает on_result(0) |
| KC9 | `cancel_op` | cancel(observer) → observer получает on_result с кол-вом отменённых |
| KC10 | `cancel_fd` | cancel_fd(observer, fd) → отмена операций на fd |
| KC11 | `observer_runner_identity` | _runner_identity проставляется из await_suspend |
| KC12 | `observer_auto_identity` | Если null → берётся из runner::get() |
| KC13 | `iovec_allocate_free` | iovec_allocate(n) → iovec_deallocate(iov) |
| KC14 | `iovec_pool_allocate` | iovec_pool_allocate(len) → массив iovec |
| KC15 | `register_files` | register_files / unregister_files |
| KC16 | `register_files_update` | register_files_update обновляет fd по индексу |

---

### 3.13 services/clock.h

#### `ClockFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| CL1 | `clock_subscribe_zero` | subscribe(node, 0ms) → немедленный reattach |
| CL2 | `clock_subscribe_short` | subscribe(node, 1ms) → задержка ~1ms |
| CL3 | `clock_subscribe_long` | subscribe(node, 1000ms) → верхний уровень dial |
| CL4 | `clock_detach_record` | detach_record(node) → reattach + remove |
| CL5 | `clock_ping` | ping() освобождает истекшие таймеры |
| CL6 | `clock_current_time` | current_time() монотонно возрастает |
| CL7 | `clock_current_time_cached` | current_time() обновляется каждые 16 вызовов |
| CL8 | `multi_dial_select_dial` | Правильный выбор уровня для разных duration |
| CL9 | `dial_release_ticks` | release_ticks освобождает слоты по arrow |
| CL10 | `dial_release_ticks_limit` | _release_counter ограничивает кол-во освобождений |
| CL11 | `dial_migrate` | migrate каскадирует записи на верхний dial |
| CL12 | `dial_inject_node` | inject_node добавляет существующую запись |
| CL13 | `time_slot_release_all` | release_slot() без лимита — все записи |
| CL14 | `time_slot_release_limited` | release_slot(N) — не более N записей |
| CL15 | `clock_record_move` | Move-конструктор и оператор |
| CL16 | `multi_dial_empty` | empty() → true когда нет записей |
| CL17 | `multi_dial_adjust` | adjust() обновляет _current_ts и _release_counter |
| CL18 | `multi_dial_stopped` | _stopped флаг при empty |

---

### 3.14 futures/timeout.h

#### Расширение `TimerFixture`

| # | Тест | Что проверяет |
|---|------|--------------|
| T1 | `timeout_zero` | timeout(0ms) → почти мгновенное завершение |
| T2 | `timeout_negative` | Отрицательный timeout → поведение |
| T3 | `expire_past` | expire(время в прошлом) → мгновенное завершение |
| T4 | `expire_future` | expire(время в будущем) → задержка |
| T5 | `timeout_cancel_before_clock` | cancel до того как clock обработал |
| T6 | `timeout_cancel_after_clock` | cancel после того как таймер истёк |
| T7 | `timeout_router_cancel_reattach` | cancel() возвращает ноду в runner |
| T8 | `timeout_multiple_concurrent` | 100 одновременных таймеров → все завершаются |

---

### 3.15 futures/channel.h

#### Расширение `ChannelFixture`

| # | Тест | Что проверяет |
|---|------|--------------|
| CH1 | `push_pull_single` | push → pull возвращает то же значение |
| CH2 | `push_full` | push на полный канал → false |
| CH3 | `pending_push` | pending_push ждёт освобождения |
| CH4 | `pending_push_rvalue` | pending_push(T&&) |
| CH5 | `operator_left_shift` | ch << val → push |
| CH6 | `channel_empty` | empty() на пустом/непустом |
| CH7 | `pull_suspends` | pull на пустом канале → корутина суспендится |
| CH8 | `channel_mpsc` | Несколько producer-ов, один consumer |
| CH9 | `channel_mpmc` | Несколько producer-ов и consumer-ов |
| CH10 | `channel_st` | channel_st (single-thread) вариант |
| CH11 | `channel_bounded` | bounded шина: push блокируется при заполнении |
| CH12 | `channel_dyn` | dyn шина: динамическое расширение |
| CH13 | `channel_router_cancel` | cancel во время ожидания pull |
| CH14 | `channel_notify` | notify пробуждает ожидающих |
| CH15 | `channel_router_redirect` | redirect сохраняет waiter |
| CH16 | `channel_sp_sc` | SPSC режим |

---

### 3.16 futures/cutex.h

#### Расширение `CutexFixture`

| # | Тест | Что проверяет |
|---|------|--------------|
| CX1 | `cutex_try_lock_free` | try_lock() на свободном → true |
| CX2 | `cutex_try_lock_busy` | try_lock() на занятом → false |
| CX3 | `cutex_capture_suspends` | capture() на занятом → корутина суспендится |
| CX4 | `cutex_sync_wakes` | sync() пробуждает следующего ожидающего |
| CX5 | `cutex_proxy_double_capture` | Повторный capture() без sync() → logic_error |
| CX6 | `cutex_proxy_double_sync` | Повторный sync() — no-op |
| CX7 | `cutex_proxy_destructor_sync` | ~proxy() вызывает sync() автоматически |
| CX8 | `cutex_proxy_volatile` | volatile proxy корректно работает |
| CX9 | `cutex_notify_rescheduling` | _rescheduling = true → waiter мигрирует |
| CX10 | `cutex_pending_notify` | pending_notify: если notify() не удался → повтор |
| CX11 | `cutex_cancel_in_queue` | cancel задачи в очереди ожидания → нода остаётся, sync() пробуждает следующего |
| CX12 | `cutex_cancel_after_capture` | cancel после захвата → proxy деструктор освобождает |
| CX13 | `cutex_guard_raii` | ace::guard + co_await capture → автоматический sync при разрушении |
| CX14 | `cutex_high_contention` | 100+ корутин на одном cutex → порядок FIFO |
| CX15 | `cutex_notify_no_waiter` | notify() без ожидающих → false |
| CX16 | `cutex_router_redirect` | redirect сохраняет waiter в очередь |

---

### 3.17 Управление задачами

#### Расширение `SpawnFixture`

| # | Тест | Что проверяет |
|---|------|--------------|
| SP1 | `spawn_await_suspend` | spawn не суспендит (await_suspend → false) |
| SP2 | `spawn_returns_handle` | co_await spawn(task) → async_handle |
| SP3 | `spawn_on_specific_runner` | spawn с указанием runner |
| SP4 | `post_await_suspend` | post суспендит (await_suspend → true) |
| SP5 | `post_uses_attach_front` | post → задача в начало очереди |
| SP6 | `reattach_changes_runner` | reattach(runner*) → _runner обновляется |
| SP7 | `reattach_router_redirect` | reattach_router::redirect обновляет _runner + reattach |
| SP8 | `roaming_true_allows_migration` | roaming(true) → _roaming = true |
| SP9 | `roaming_false_prevents` | roaming(false) → _roaming = false |
| SP10 | `polling_true_to_vortex` | polling(true) → задача идёт в _vortex_pool |
| SP11 | `polling_false_to_normal` | polling(false) → задача в _pool |
| SP12 | `get_runner_inside_runner` | get_runner внутри runner → не-nullptr |
| SP13 | `get_runner_outside_runner` | get_runner вне runner → nullptr |

---

### 3.18 ace_entry.cpp

#### `EntryFixture` (новая, может требовать отдельной сборки)

| # | Тест | Что проверяет |
|---|------|--------------|
| E1 | `weak_main_falls_back` | Без user main → вызывается co_main |
| E2 | `user_main_overrides` | User main определён → вызывается он |
| E3 | `co_main_entry` | co_main получает argc/argv |

Примечание: тесты `ace_entry.cpp` требуют сборки с `-Dace_entry=true` и отдельного тестового executable.

---

### 3.19 fs.h

#### Расширение `FsFixture`

| # | Тест | Что проверяет |
|---|------|--------------|
| FS1 | `file_open_rdonly` | open_rdonly() → file_link |
| FS2 | `file_open_wronly` | open_wronly() → file_link |
| FS3 | `file_open_rewrite` | open_rewrite() → file_link |
| FS4 | `file_open_fail` | open() несуществующего файла на чтение → error |
| FS5 | `file_write` | write() через file_link |
| FS6 | `file_writeln` | writeln() через file_link |
| FS7 | `file_read` | read() через file_link |
| FS8 | `file_read_str` | read_str() через file_link |
| FS9 | `file_output_action` | output_action использует kernel_controller::writev |
| FS10 | `file_input_action` | input_action использует read_query |

---

### 3.20 console.h

#### `ConsoleFixture` (новая фикстура)

| # | Тест | Что проверяет |
|---|------|--------------|
| CN1 | `println_format` | println(fmt, args...) |
| CN2 | `println_string_view` | println(string_view) |
| CN3 | `println_empty` | println() — пустая строка |
| CN4 | `print_format` | print(fmt, args...) без newline |
| CN5 | `print_string_view` | print(string_view) |
| CN6 | `stdin_link` | stdin_link() возвращает валидный link |
| CN7 | `stdout_link` | stdout_link() возвращает валидный link |
| CN8 | `input_link` | input_link() для stdin |
| CN9 | `output_link` | output_link() для stdout |

---

## Кросс-механизмы

Тесты взаимодействия нескольких подсистем одновременно.

#### `CrossMechanicFixture` (новая фикстура)

| # | Тест | Взаимодействие |
|---|------|---------------|
| X1 | `cancel_spawned_with_timeout` | spawn → timeout → cancel: проверка что cancel освобождает таймер и корутину |
| X2 | `cancel_spawned_with_channel` | spawn → channel.pull → cancel: waiter удаляется из канала |
| X3 | `cancel_spawned_with_cutex` | spawn → cutex.capture → cancel: proxy освобождает cutex |
| X4 | `cancel_spawned_with_recv` | spawn → recv → cancel: io_uring запрос отменяется |
| X5 | `reattach_during_timeout` | timeout → reattach на другой раннер → таймер срабатывает на новом раннере |
| X6 | `roaming_with_spawn_and_cutex` | roaming + spawn на другом раннере + cutex: гонка с миграцией |
| X7 | `polling_with_timeout` | polling(true) → timeout → задача в vortex + clock |
| X8 | `or_compose_with_cancel` | timeout or recv → cancel ор-композиции → оба observer-а отменяются |
| X9 | `and_compose_with_cancel` | spawn and channel → cancel → оба observer-а отменяются |
| X10 | `pipe_with_channel` | pusher >> channel.pull: значение передаётся через канал |
| X11 | `spawn_post_interaction` | spawn + post одновременно → порядок выполнения |
| X12 | `channel_with_timeout` | channel.pull or timeout → гонка |
| X13 | `cutex_with_timeout` | cutex.capture or timeout → гонка за мьютексом |
| X14 | `cutex_with_channel` | Под cutex пишем в channel →原子арность |
| X15 | `reattach_with_cancel` | reattach во время выполнения → cancel на новом раннере |
| X16 | `dispatcher_reload_during_run` | reload() во время run() → раннеры переконфигурируются |
| X17 | `multi_runner_cutex` | 4 раннера, 100 корутин на cutex → счётчик корректен |
| X18 | `multi_runner_channel` | 4 раннера, producer/consumer через канал |
| X19 | `multi_runner_spawn` | spawn на разных раннерах, join всех |
| X20 | `interrupt_during_timeout` | interrupt() во время timeout → корутина получает e_break |
| X21 | `terminate_during_recv` | terminate() во время recv → все раннеры останавливаются |
| X22 | `socket_echo_multi_client` | 2+ клиентов на одном сервере |
| X23 | `socket_echo_with_cancel` | recv or timeout → cancel выигравшей ветки |
| X24 | `fs_write_read_cycle` | write → read → verify |
| X25 | `stress_spawn_cancel` | 1000 spawn → cancel всех → нет утечек |

---

## Обновление сборки

### meson_options.txt
Добавить строку:
```
option('coverage', type: 'boolean', value: false, description: 'Enable gcov/lcov code coverage')
```

### meson.build
В блоке `if tests_enabled`, после `compile_args` добавить:
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

В `executable(...)` добавить:
```python
link_args: coverage_link_args,
```

### discover_tests.py
Исправить: добавить поддержку флага `--list-only` чтобы list-режим не запускал тесты полного прохода. Если передан `--list-only` — выполнить `--gtest_list_tests`. Иначе — обычный запуск с `--gtest_filter`.

### scripts/coverage.sh
```bash
#!/bin/bash
BUILD=${1:-build}
meson setup "$BUILD" -Dtests=true -Dcoverage=true --reconfigure
ninja -C "$BUILD" ace_tests
./"$BUILD"/ace_tests
lcov -c -d "$BUILD" -o coverage.info --rc lcov_branch_coverage=1
lcov --remove coverage.info '/usr/*' '*/subprojects/*' '*/tests/*' -o coverage_filtered.info
genhtml coverage_filtered.info -o coverage_report --branch-coverage
echo "Report: coverage_report/index.html"
```

---

## Карта fixture-классов (итоговая)

| Fixture | Наследует | Новые/Расширение | Тестов |
|---------|----------|-----------------|--------|
| `BaseFixture` | `::testing::Test` | существующий | — |
| `ContextFixture` | `BaseFixture` | +A1..A20 (async) | 5→25 |
| `ChannelFixture` | `BaseFixture` | +CH1..CH16 (channel) | 1→17 |
| `TimerFixture` | `BaseFixture` | +T1..T8 (timeout), +C1..C17 (compose) | 5→30 |
| `TimerParallelFixture` | `BaseFixture` | без изменений | 1 |
| `CutexFixture` | `BaseFixture` | +CX1..CX16 (cutex) | 4→20 |
| `SpawnFixture` | `BaseFixture` | +SP1..SP13, +AH1..AH8 | 6→27 |
| `SocketEchoFixture` | `BaseFixture` | +N1..N35 (net) | 2→37 |
| `FsFixture` | `BaseFixture` | +FS1..FS10 | 1→11 |
| `QueueFixture` | `BaseFixture` | **новый** | 10 |
| `OmniptrFixture` | `BaseFixture` | **новый** | 13 |
| `IdAllocFixture` | `BaseFixture` | **новый** | 4 |
| `MovingAverageFixture` | `BaseFixture` | **новый** | 4 |
| `FutureTraitsFixture` | `::testing::Test` | **новый** (compile-time) | 8 |
| `PromiseTraitsFixture` | `BaseFixture` | **новый** | 12 |
| `RouterSlotFixture` | `BaseFixture` | **новый** | 10 |
| `VortexFixture` | `BaseFixture` | **новый** | 10 |
| `ControlBlockFixture` | `BaseFixture` | **новый** | 18 |
| `RunnerFixture` | `BaseFixture` | **новый** | 21 |
| `DispatcherFixture` | `BaseFixture` | **новый** | 14 |
| `SignalFixture` | `BaseFixture` | **новый** | 6 |
| `IoBufferFixture` | `BaseFixture` | **новый** | 23 |
| `IoQueryFixture` | `BaseFixture` | **новый** | 12 |
| `IoEntityFixture` | `BaseFixture` | **новый** | 9 |
| `IoHangedFixture` | `BaseFixture` | **новый** | 5 |
| `IoAnyFixture` | `BaseFixture` | **новый** | 4 |
| `KernelicFixture` | `BaseFixture` | **новый** | 16 |
| `ClockFixture` | `BaseFixture` | **новый** | 18 |
| `ConsoleFixture` | `BaseFixture` | **новый** | 9 |
| `CrossMechanicFixture` | `BaseFixture` | **новый** | 25 |
| `EntryFixture` | `::testing::Test` | **новый** (отдельный executable) | 3 |

**Итого:** 30 fixture-классов, ~350+ новых тестов (поверх существующих 24).

**Приоритет внедрения:**
1. Coverage-инструменты (мезон + скрипты)
2. `QueueFixture` + `OmniptrFixture` + `MovingAverageFixture` (tools — база)
3. `FutureTraitsFixture` + `PromiseTraitsFixture` + `RouterSlotFixture` (traits — база)
4. `ControlBlockFixture` + `RunnerFixture` (core — база)
5. `IoBufferFixture` + `IoQueryFixture` + `IoEntityFixture` + `IoHangedFixture` (I/O — база)
6. `KernelicFixture` + `ClockFixture` (services)
7. Расширение существующих фикстур (async, channel, timer, spawn, cutex, socket, fs)
8. `VortexFixture` + `DispatcherFixture` + `SignalFixture` (инфраструктура)
9. `CrossMechanicFixture` (интеграционные)
10. `EntryFixture` (entry-point, опционально)
