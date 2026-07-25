# ACE Framework — Test Coverage Plan

Цель: 95-100% покрытия кодовой базы + проверка всех механик и их взаимодействий.

## Требования к генерации тестов (agent instructions)

1. **Приоритет исправления проекта над тестами**: если сгенерированные тесты не проходят из-за ошибок в проекте — исправлять исходный код, а не «костылить» тесты. Тесты должны проверять корректное поведение, а не маскировать баги.

2. **Обязательные комментарии**:
   - Каждый `TEST_F` / `TEST` должен предваряться комментарием, описывающим **что конкретно проверяет тест**.
   - В теле теста должны быть комментарии в формате ответа на вопрос: **«Почему я это проверяю, и почему я это проверяю именно так?»**.

3. **Актуализация плана**: после выполнения работ по плану агент обязан обновить TEST_PLAN.md — отметить реализованные тесты (✅), обновить карту фикстур и индексацию.

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

| Модуль | Покрыто | Не покрыто |
|--------|---------|------------|
| `core/tools/queue.h` | 10%→90% | ~~slab_mempool grow/free, q_node::remove, queue::pop/unlink/remove_node, move ctor~~ ✅ |
| `core/tools/omniptr.h` | 70%→85% | ~~operator&~~ (баг const-correctness), wrong_type_cast (compile-time) |
| `core/tools/id_alloc.h` | 0%→80% ✅ | ~~весь модуль~~ ✅, исчерпание пула |
| `core/tools/moving_average.h` | 80%→95% ✅ | ~~краевые случаи~~ ✅ |
| `core/tools/lifetime.h` | 50%→80% ✅ | ~~untrack(), mark()~~ ✅ |
| `core/traits/future.h` | 60%→95% ✅ | ~~concepts, type traits~~ ✅ |
| `core/traits/promise.h` | 70%→80% | ~~automaton tag, return_traits краевые случаи~~ частично, yield_value, operator_delete, operator_delete_sized |
| `core/traits/routing.h` | 60%→95% ✅ | ~~router_slot::operator=, release(), reset()~~ ✅, router_slot_size_limit (compile-time) |
| `core/traits/vortex.h` | 50% | detach_set/detach_get, respawn, vortex() цикл с сигналами, inspect() |
| `core/control.h` | 40%→90% ✅ | ~~disown, watch/unwatch, is_untracked, is_disowned, get_block_from_address, forward~~ ✅, handle_cancel с router |
| `core/async.h` | 60%→75% | ~~~async c router, prefetch, track, automaton paths, observe edge cases~~ ✅, release_waiters, release_router, await_ready_done/with_router/resumable, await_suspend_propagates, await_resume_void/typed, get_current_pool |
| `core/runner.h` | 60%→70% | ~~velocity, clear_velocity, empty, runner_move~~ ✅, reattach_front, yank_vortex, run цикл >128 задач, fetch_task_node_local/insert/empty |
| `core/dispatcher.h` | 50%→65% | ~~reload, interrupt/terminate~~ ✅, worker_round, round_robin, fetch_config, schedule_specific_runner, многопоточное взаимодействие |
| `core/signal.h` | 0%→80% ✅ | ~~весь модуль~~ ✅ |
| `core/compose.h` | 40%→55% | ~~or_await_left_wins, and_await_both_succeed~~ ✅, or_await_composed, compose(), observer(), все router-ы |
| `core/async_handle.h` | 50%→65% | ~~join после cancel, done~~ ✅, join_handler::await_ready/suspend/resume, join_handler_router |
| `io.h` | 30%→45% | ~~io::buffer (append/clone/clear/move/formatter)~~ ✅, io::guard RAII close, io::hanged, io::any, query_router::cancel |
| `net.h` | 20% | UDP (sendto/recv), connected UDP, sendmsg/recvmsg, все bind/connect/accept overloads, error-пути, connection_link, io::caster |
| `services/kernelic.h` | 30% | cancel/cancel_fd/nop, multishot, overflow buffer (>4096), ring params, iovec allocator |
| `services/clock.h` | 30% | multi_dial уровни 2+, detach_record реаттач, dial::migrate, time_slot::release_slot(limit) |
| `futures/timeout.h` | 60%→70% | ~~timeout нулевой~~ ✅, expire краевые случаи, cancel на разных стадиях |
| `futures/channel.h` | 40%→60% | ~~push/pull, operator<<, empty, MPSC~~ ✅, pending_push, channel_st, allocation_type/access_mode варианты |
| `futures/cutex.h` | 50%→70% | ~~try_lock, proxy double capture/sync/destructor, rescheduling~~ ✅, notify с rescheduling, pending_notify цикл, cutex_cancel |
| `futures/spawn.h` | 50%→65% | ~~spawn_returns_handle~~ ✅, spawn_on_specific_runner, await_suspend |
| `futures/post.h` | 30%→55% | ~~post_uses_attach_front~~ ✅, await_suspend |
| `futures/reattach.h` | 30% | reattach_router::redirect, await_ready |
| `futures/roaming.h` | 30%→60% ✅ | ~~roaming флаг~~ ✅, roaming + spawn/post взаимодействие |
| `futures/polling.h` | 30%→60% ✅ | ~~polling флаг~~ ✅, polling + vortex взаимодействие |
| `futures/get_runner.h` | 50%→65% ✅ | ~~get_runner внутри runner~~ ✅, get_runner вне runner |
| `ace_entry.cpp` | 0% | весь модуль (если собран) |
| `fs.h` | 30%→50% ✅ | ~~file_write_read, file_open_fail~~ ✅, open_query краевые случаи, file_link input_action/read |
| `console.h` | 30%→60% ✅ | ~~println, print~~ ✅, stdin input, stdout write/writeln все перегрузки |

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

#### `vortex.h` — `vortex_fixture` (не реализована)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| V1 | `vortex_touch_spawns` | Первый touch() создаёт инстанс и spawn-ит vortex | ⬜ |
| V2 | `vortex_detach_reattach` | После detach_set(true) → touch() делает respawn | ⬜ |
| V3 | `vortex_signal_break` | Сигнал e_break → vortex приостанавливается | ⬜ |
| V4 | `vortex_signal_shutdown` | Сигнал e_shutdown → vortex завершается | ⬜ |
| V5 | `vortex_signal_idle` | Сигнал e_idle → vortex продолжает работу | ⬜ |
| V6 | `vortex_inspect` | inspect() возвращает инстанс без respawn | ⬜ |
| V7 | `vortex_thread_local` | e_thread_local: разные потоки — разные инстансы | ⬜ |
| V8 | `vortex_thread_shared` | e_thread_shared: все потоки — один инстанс | ⬜ |
| V9 | `vortex_promise_ping` | is_vortex_promise: ping() возвращает promise<bool> | ⬜ |
| V10 | `vortex_routine_ping` | is_vortex_routine: ping() возвращает bool | ⬜ |

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
| RN10 | `yank_polling` | yank() задачи с _polling → vortex_pool | ⬜ |
| RN11 | `yank_vortex` | yank_vortex() обрабатывает vortex-задачи | ⬜ |
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
| S5 | `signal_in_vortex` | Сигнал e_shutdown останавливает vortex | ⬜ |
| S6 | `signal_break_in_vortex` | Сигнал e_break приостанавливает vortex | ⬜ |

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
| C14 | `or_await_composed_3` | or(a, b, c) — три future | ⬜ (баг compose.h с omniptr::operator->) |
| C15 | `and_await_composed_3` | and(a, b, c) — три future | ⬜ |
| C16 | `or_await_cancel_observer` | cancel одного из observer-ов в or | ⬜ |
| C17 | `or_await_router_cancel` | cancel во время гонки | ⬜ |

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

---

### 3.10 io.h

#### `io_buffer_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| B1 | `buffer_expand` | expand(len) выделяет память, возвращает ненулевой указатель | ✅ |
| B2 | `buffer_expand_multiple` | Несколько expand() — корректный суммарный len() | ✅ |
| B3 | `buffer_append_format` | append(fmt, args...) добавляет форматированную строку | ⬜ |
| B4 | `buffer_append_string_view` | append(string_view) → as<string>() | ✅ |
| B5 | `buffer_append_raw` | append(void*, void*) | ⬜ |
| B6 | `buffer_append_vector` | append(vector<T>) | ⬜ |
| B7 | `buffer_append_array` | append(array<T,N>) | ⬜ |
| B8 | `buffer_append_span` | append(span<T>) | ⬜ |
| B9 | `buffer_prepend_format` | prepend(fmt, args...) добавляет в начало | ⬜ |
| B10 | `buffer_prepend_string_view` | prepend(string_view) | ⬜ |
| B11 | `buffer_appendln` | appendln добавляет строку + \n | ⬜ |
| B12 | `buffer_assemble` | assemble() → msghdr* с правильным iov_len | ⬜ |
| B13 | `buffer_assemble_once` | Повторный assemble() — возвращает тот же msghdr* | ⬜ |
| B14 | `buffer_disassemble` | disassemble() сбрасывает msg_iov | ⬜ |
| B15 | `buffer_shape` | shape(len) обрезает хвост до len | ⬜ |
| B16 | `buffer_shape_single` | shape на единственном чанке → замена чанка | ⬜ |
| B17 | `buffer_clone` | clone() создаёт копию с теми же данными | ✅ |
| B18 | `buffer_clear` | clear() освобождает все чанки, len() = 0 | ✅ |
| B19 | `buffer_move` | Move-конструктор переносит данные, исходный пуст | ✅ |
| B20 | `buffer_as_string` | as<string>() собирает все чанки в строку | ✅ |
| B21 | `buffer_as_bytes` | as<vector<byte>>() | ⬜ |
| B22 | `buffer_len` | len() возвращает суммарную длину | ✅ (через expand_multiple) |
| B23 | `buffer_formatter` | std::format("{}", buf) работает | ✅ |

#### `IoQueryFixture` (не реализована)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| IQ1-IQ12 | Все тесты io::query | ⬜ |

#### `io_entity_fixture`

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| IE1-IE9 | Все тесты io::entity + io::guard | ⬜ (частично: IE9 guard_no_runner) |

#### `IoHangedFixture` (не реализована), `IoAnyFixture` (не реализована)

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| IH1-IH5, IA1-IA4 | Все тесты | ⬜ |

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
| SP8 | `polling_true` | polling(true) → задача идёт в _vortex_pool | ✅ |
| SP9 | `get_runner_inside_runner` | get_runner внутри runner → не-nullptr | ✅ |

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

| # | Тест | Что проверяет | Статус |
|---|------|--------------|--------|
| CN1 | `println_format` | println(fmt, args...) | ⬜ |
| CN2 | `println_string_view` | println(string_view) | ✅ |
| CN3 | `println_empty` | println() — пустая строка | ✅ |
| CN4 | `print_format` | print(fmt, args...) без newline | ✅ |
| CN5 | `print_string_view` | print(string_view) | ✅ |
| CN6 | `stdin_link` | stdin_link() возвращает валидный link | ⬜ |
| CN7 | `stdout_link` | stdout_link() возвращает валидный link | ⬜ |
| CN8 | `input_link` | input_link() для stdin | ⬜ |
| CN9 | `output_link` | output_link() для stdout | ⬜ |

---

## Кросс-механизмы

Тесты взаимодействия нескольких подсистем одновременно.

#### `cross_mechanic_fixture`

| # | Тест | Взаимодействие | Статус |
|---|------|---------------|--------|
| X1 | `cancel_spawned_with_timeout` | spawn → timeout → cancel: проверка что cancel освобождает таймер и корутину | ✅ |
| X2 | `cancel_spawned_with_channel` | spawn → channel.pull → cancel: waiter удаляется из канала | ✅ |
| X3 | `cancel_spawned_with_cutex` | spawn → cutex.capture → cancel: proxy освобождает cutex | ⬜ |
| X4 | `cancel_spawned_with_recv` | spawn → recv → cancel: io_uring запрос отменяется | ⬜ |
| X5 | `reattach_during_timeout` | timeout → reattach на другой раннер → таймер срабатывает на новом раннере | ⬜ |
| X6 | `roaming_with_spawn_and_cutex` | roaming + spawn на другом раннере + cutex: гонка с миграцией | ⬜ |
| X7 | `polling_with_timeout` | polling(true) → timeout → задача в vortex + clock | ⬜ |
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

| Fixture | Наследует | Новые/Расширение | Тестов (план/факт) |
|---------|----------|-----------------|---------------------|
| `base_fixture` | `::testing::Test` | существующий | — |
| `context_fixture` | `base_fixture` | +A1..A20 (async) | 5→13 (✅ 8) |
| `channel_fixture` | `base_fixture` | +CH1..CH16 (channel) | 1→1 |
| `timer_fixture` | `base_fixture` | +T1..T9 (timeout) | 5→8 (✅ 3) |
| `timer_parallel_fixture` | `base_fixture` | без изменений | 1 |
| `cutex_fixture` | `base_fixture` | +CX1..CX16 (cutex) | 4→4 |
| `spawn_fixture` | `base_fixture` | +SP1..SP5, +AH1..AH8 | 6→6 |
| `socket_echo_fixture` | `base_fixture` | +N1..N35 (net) | 2→2 |
| `fs_fixture` | `base_fixture` | +FS1..FS11 | 1→4 (✅ 3) |
| `queue_fixture` | `::testing::Test` | **новый** | 10→10 (✅ 10) |
| `omniptr_fixture` | `::testing::Test` | **новый** | 13→12 (✅ 10) |
| `id_alloc_fixture` | `::testing::Test` | **новый** | 4→3 (✅ 3) |
| `moving_average_fixture` | `::testing::Test` | **новый** | 4→7 (✅ 7) |
| `future_traits_fixture` | `::testing::Test` | **новый** (compile-time) | 8→8 (✅ 8) |
| `promise_traits_fixture` | `base_fixture` | **новый** | 12→8 (✅ 6) |
| `router_slot_fixture` | `::testing::Test` | **новый** | 10→8 (✅ 8) |
| `vortex_fixture` | `base_fixture` | **новый** — не реализована | 10→0 |
| `control_block_fixture` | `::testing::Test` | **новый** | 18→15 (✅ 14) |
| `runner_fixture` | `base_fixture` | **новый** | 21→10 (✅ 8) |
| `dispatcher_fixture` | `base_fixture` | **новый** | 14→8 (✅ 7) |
| `signal_fixture` | `base_fixture` | **новый** | 6→4 (✅ 4) |
| `io_buffer_fixture` | `::testing::Test` | **новый** | 23→8 (✅ 8) |
| `io_query_fixture` | `base_fixture` | **новый** — не реализована | 12→0 |
| `io_entity_fixture` | `::testing::Test` | **новый** | 9→1 (✅ 0) |
| `io_hanged_fixture` | `base_fixture` | **новый** — не реализована | 5→0 |
| `io_any_fixture` | `base_fixture` | **новый** — не реализована | 4→0 |
| `kernelic_fixture` | `base_fixture` | **новый** — не реализована | 16→0 |
| `clock_fixture` | `base_fixture` | **новый** — не реализована | 18→0 |
| `console_fixture` | `::testing::Test` | **новый** | 9→4 (✅ 4) |
| `cross_mechanic_fixture` | `base_fixture` | **новый** | 25→14 (✅ 12) |
| `entry_fixture` | `::testing::Test` | **новый** (отдельный executable) — не реализована | 3→0 |
| `spawn_extra_fixture` | `base_fixture` | **новый** (расширение spawn) | —→8 (✅ 8) |
| `compose_extra_fixture` | `base_fixture` | **новый** (расширение compose) | —→3 (✅ 3) |
| `channel_extra_fixture` | `base_fixture` | **новый** (расширение channel) | —→4 (✅ 4) |
| `cutex_extra_fixture` | `base_fixture` | **новый** (расширение cutex) | —→5 (✅ 5) |
| `get_runner_fixture` | `base_fixture` | **новый** | —→1 (✅ 1) |

**Итого:** 35 fixture-классов, 175 тестов (было 24, добавлен 151).

**Бенчмарки:** 6 тестов перенесены в `benchmarks/` (см. `BUGS_AND_BENCHMARKS.md`).
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
| 25–70 | `base_fixture` | Базовый |
| 76–89 | `context_fixture` | async |
| 95–117 | `channel_fixture` | channel |
| 123–180 | `timer_fixture` | timeout |
| 187–209 | `timer_parallel_fixture` | timeout (parallel) |
| 215–311 | `cutex_fixture` | cutex |
| 317–450 | `spawn_fixture` | spawn/post/compose |
| 458–533 | `socket_echo_fixture` | net |
| 539–545 | `fs_fixture` | fs |
| 549–556 | `queue_fixture` | tools |
| 558 | `omniptr_fixture` | tools |
| 560 | `id_alloc_fixture` | tools |
| 562 | `moving_average_fixture` | tools |
| 564 | `future_traits_fixture` | traits |
| 570–594 | `promise_traits_fixture` | traits |
| 600–629 | `router_slot_fixture` | traits |
| 631–633 | `signal_fixture` | signal |
| 638–680 | `control_block_fixture` | control |
| 685–698 | `runner_fixture` | runner |
| 704–725 | `dispatcher_fixture` | dispatcher |
| 729 | `io_buffer_fixture` | io |
| 731 | `io_entity_fixture` | io |
| 735 | `console_fixture` | console |
| 741–750 | `cross_mechanic_fixture` | integration |
| 755–768 | `spawn_extra_fixture` | futures |
| 775–793 | `compose_extra_fixture` | compose |
| 799–809 | `channel_extra_fixture` | channel |
| 815–836 | `cutex_extra_fixture` | cutex |
| 840–842 | `get_runner_fixture` | futures |

### Расположение тестов в `tests/tests.cpp`

| Строки | Fixture | Количество тестов |
|--------|---------|-------------------|
| 9–36 | `context_fixture` | 4 (существующие) |
| 42–47 | `context_fixture` (runner) | 1 |
| 49–74 | `timer_fixture` | 3 |
| 76–80 | `fs_fixture` | 1 (существующий) |
| 86–93 | `channel_fixture` | 1 |
| 95–146 | `timer_fixture` | 2 |
| 148–169 | `cutex_fixture` | 2 |
| 171–206 | `timer_parallel_fixture` | 1 |
| 208–220 | `socket_echo_fixture` | 2 |
| 226–298 | `spawn_fixture` | 6 |
| 300–354 | `cutex_fixture` | 2 |
| 358–510 | `queue_fixture` | 10 |
| 512–646 | `omniptr_fixture` | 10 (+2 lifetime) |
| 648–676 | `id_alloc_fixture` | 3 |
| 678–750 | `moving_average_fixture` | 7 |
| 752–808 | `future_traits_fixture` | 8 |
| 810–896 | `promise_traits_fixture` | 8 |
| 898–1004 | `router_slot_fixture` | 8 |
| 1006–1124 | `control_block_fixture` | 15 |
| 1126–1178 | `signal_fixture` | 4 |
| 1180–1260 | `runner_fixture` | 8 |
| 1262–1364 | `dispatcher_fixture` | 7 |
| 1366–1450 | `io_buffer_fixture` | 7 |
| 1452–1464 | `io_entity_fixture` | 1 |
| 1466–1498 | `console_fixture` | 4 |
| 1500–1554 | `context_fixture` (async ext) | 8 |
| 1556–1670 | `spawn_extra_fixture` | 8 |
| 1672–1732 | `compose_extra_fixture` | 3 |
| 1734–1850 | `channel_extra_fixture` | 4 |
| 1852–1964 | `cutex_extra_fixture` | 5 |
| 1966–1980 | `get_runner_fixture` | 1 |
| 1982–2348 | `cross_mechanic_fixture` | 14 |
| 2350–2430 | `timer_fixture` (timeout ext) | 3 |
| 2432–2500 | `fs_fixture` (fs ext) | 3 |
