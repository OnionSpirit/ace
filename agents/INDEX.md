# ACE Framework - Project Navigation Index

Дата актуализации: 2026-08-23.

Этот документ описывает текущую архитектуру, публичные API, инварианты и файловую
карту ACE. Он намеренно ссылается на файлы и символы, а не на номера строк:
реализация header-only и быстро меняется. Пользовательский quick start и примеры
находятся в `README.md`; правила тестирования, benchmarks и известные проблемы -
в `TESTING.md`, `BENCHMARKS.md` и `ISSUES.md`.

## Оглавление

1. [Быстрый старт и сборка](#быстрый-старт-и-сборка)
2. [Точка входа и конфигурация](#точка-входа-и-конфигурация)
3. [Корутины](#корутины)
4. [Комбинаторы](#комбинаторы)
5. [Управление задачами](#управление-задачами)
6. [Каналы и cutex](#каналы-и-cutex)
7. [I/O ownership](#io-ownership)
8. [Сеть](#сеть)
9. [Файлы и консоль](#файлы-и-консоль)
10. [Dispatcher, runner и routing](#dispatcher-runner-и-routing)
11. [io_uring и clock](#io_uring-и-clock)
12. [Service, arena и tools](#service-arena-и-tools)
13. [Критические ограничения](#критические-ограничения)
14. [Файловая карта](#файловая-карта)
15. [Тесты, сборка и coverage](#тесты-сборка-и-coverage)

## Быстрый старт и сборка

ACE - header-only C++ coroutine framework для Linux поверх `io_uring`. Один
`runner` исполняется на поток; `dispatcher` распределяет задачи между runners.

```cpp
#include <ace/ace.h>

ace::task hello() {
    co_return;
}

int main() {
    ace::schedule(hello());
    ace::run();
}
```

Основные команды:

```bash
meson setup build
meson compile -C build

meson setup build-tests -Dtests=true
meson compile -C build-tests
meson test -C build-tests
```

`ace::run()` блокирует вызывающий поток до завершения event loop. Проект использует
C++23 для своей test-конфигурации; требования к consumer build и актуальные
пользовательские команды следует сверять с `README.md` и `meson.build`.

## Точка входа и конфигурация

### Прямой вход

`include/ace/ace.h` экспортирует основной путь `ace::schedule(task)` +
`ace::run()`. `schedule()` принимает `ace::task`; typed `ace::async<T>` нужно
передавать через предусмотренный runner path или `ace::task_wrap()`.

### `co_main`

`include/ace/core/entry.h` содержит weak-entry integration:

```cpp
#include <ace/ace.h>

auto co_main(int argc, char** argv) -> ace::entry {
    co_return 0;
}
```

`ace::entry` является async-результатом с `entry_result::code`. Конкретный режим
entry выбирается Meson option `ace_entry`; fallback executable проверяется
отдельным tooling test, когда этот режим включён.

### Конфигурация

`include/ace/core/config.h` определяет `ace::cfg::config`, глобальный
`ace::cfg::g_config`, compile-time `ace_param<Tag>` и `detail::resolve<Tag>()`.
`ace::cfg::init()` из `include/ace/core/entry.h` применяет параметры перед стартом
dispatcher.

Ключевые поля:

| Поле | Назначение |
|------|------------|
| `_runners_amount` | Количество runners; по умолчанию один. |
| `_emergency_default` | Выполнять backup callbacks при необработанном исключении. |
| `_max_allocation_size` | Общий лимит arena; `0` отключает лимит. |
| `_breach_memory_limit` | Разрешить transient malloc fallback вместо `std::bad_alloc`. |

После изменения `_runners_amount` вызывается `ace::reload()`.

## Корутины

Основные определения находятся в `include/ace/core/async.h` и
`include/ace/core/traits/promise.h`.

| Тип | Rule | Старт и назначение |
|-----|------|--------------------|
| `ace::async<T>` | `lazy_rule` | `initial_result()` возвращает `std::suspend_always`; запускается ожиданием или планированием. |
| `ace::promise<T>` | `eager_rule` | `initial_result()` возвращает `std::suspend_never`; тело начинает выполняться при вызове. |
| `ace::automaton<T>` | `automaton_rule` | `initial_result()` возвращает `std::suspend_always`; ленивый источник `co_yield` значений. |
| `ace::task` | `lazy_rule<void>` | Move-only lazy void coroutine, основной тип для `schedule()`. |
| `ace::suspend` | - | Алиас точки приостановки `std::suspend_always`. |

`ace::async<T>` и остальные `async<..., Rule>` move-only. Promise frame хранит
runner/router state, waiters, lifecycle status, roaming/polling flags и return или
yield storage. `initial_suspend()` делегирует `Rule::initial_result()`, а
`final_suspend()` сохраняет frame до корректного освобождения observers.

### Automaton

`automaton_rule::initial_result()` возвращает `std::suspend_always`: automaton не
выполняет тело при одном только создании. После spawn внешний
`async_handle<T, automaton_rule>` управляет продвижением и потреблением значений:

- `co_await handle.ping()` продвигает automaton до следующего `co_yield` или
  terminal result и потребляет одно доступное значение;
- `co_await handle.join()` реализует ping + cancel для активного automaton;
- деструктор специализированного handle отменяет ещё активный automaton;
- terminal `co_return` доступен через тот же ping/join result path.

```cpp
ace::automaton<int> sequence() {
    co_yield 1;
    co_yield 2;
    co_return 3;
}

ace::task consume_sequence() {
    auto handle = co_await ace::spawn(sequence());
    auto first = co_await handle.ping();
    auto second = co_await handle.ping();
    auto final = co_await handle.ping();
    co_return;
}
```

`include/ace/core/async_handle.h` содержит `join_handler`, `ping_handler` и
`automaton_join_handler`. Обычный `join()` ждёт terminal status; automaton join
читает следующее значение и применяет cancellation contract.

### Lifecycle и control block

`include/ace/core/control.h` содержит intrusive `control_block` и copyable
`control_block_handle`. Lifecycle различает initialized, suspended, yielded,
finished, failed и canceled states. `observe()` создаёт handle для join/cancel,
router forwarding и чтения return/yield value. Coroutine frame уничтожается после
освобождения ownership и всех наблюдателей.

## Комбинаторы

`include/ace/core/compose.h` реализует:

| Операция | Поведение |
|----------|-----------|
| `left or right` | Race; отменяет проигравшего observer. |
| `left and right` | Ждёт оба результата. |
| `sender >> responder` | Передаёт результат следующей функции или coroutine function. |

Типы результатов `or`:

| Операнды | Результат |
|----------|-----------|
| `void`, `void` | `int` с индексом победителя. |
| `T`, `void` или `void`, `T` | `std::optional<T>`. |
| `T`, `U` | `std::variant<T, U>`. |

Типы результатов `and`:

| Операнды | Результат |
|----------|-----------|
| `void`, `void` | `void`. |
| `T`, `void` или `void`, `T` | `T`. |
| `T`, `U` | `std::tuple<T, U>`. |

Variadic chains из трёх и более operands представлены
`or_await_composed`/`and_await_composed`. Для capturing обычного callable в
`operator>>` forwarded результат принимается по значению или `const&`, но не по
изменяемой `T&`.

## Управление задачами

### Dispatcher API

`include/ace/core/dispatcher.h` экспортирует:

| Функция | Назначение |
|---------|------------|
| `ace::schedule(task&&, runner*)` | Поставить task в event loop, опционально на конкретный runner. |
| `ace::run()` | Запустить блокирующий event loop. |
| `ace::empty()` | Проверить, idle ли все runners. |
| `ace::reload()` | Применить новое количество runners. |
| `ace::interrupt()` | Отправить `e_break`. |
| `ace::terminate()` | Отправить `e_shutdown`. |
| `ace::reset_signal()` | Очистить signal pipe. |

### Spawn и post

`include/ace/futures/spawn.h` ставит coroutine в конец очереди и возвращает
`async_handle`. `include/ace/futures/post.h` использует front queue для
приоритетного запуска. Typed async поддерживается runner carrier path; valued
`join()` возвращает `std::optional<T>`.

```cpp
ace::async<int> calculate() {
    co_return 42;
}

ace::task parent() {
    auto handle = co_await ace::spawn(calculate());
    auto result = co_await handle.join();
    co_return;
}
```

Другие futures:

| API | Назначение |
|-----|------------|
| `ace::reattach(runner*)` | Мигрировать coroutine на указанный runner. |
| `ace::roaming(bool)` | Разрешить или запретить runner migration. |
| `ace::get_runner{}` | Получить текущий runner. |
| `ace::polling(bool)` | Перевести task в low-priority service pool. |
| `ace::backup(payload)` | Зарегистрировать LIFO callback/task на cancellation. |
| `ace::insure(payload)` | Защитить следующую await/yield operation. |
| `ace::emergency(bool)` | Управлять backup при exception. |

## Каналы и cutex

### Channel

`include/ace/futures/channel.h` реализует bounded cooperative channel с режимами
SPSC, MPSC и MPMC. Основные операции: synchronous `push`, awaitable `pull`,
`pending_push` и stream-like `operator<<`/`operator>>`. Короткие aliases `local`,
`bridge`, `funnel` и `bus` зависят от include-order правила, описанного ниже.

### Cutex

`include/ace/futures/cutex.h` реализует cooperative userspace mutex. Fast path -
atomic counter без syscall; slow path помещает suspended task в roaming MPSC
queue. `cutex::proxy` обеспечивает RAII, `capture()` сохраняет runner, а `sync()`
разрешает migration к runner владельца.

Критический race между неуспешным `try_lock()` и enqueue закрывает
`pending_notify()`: если release видит ожидающего, но queue ещё пуста, отдельная
task повторяет notification. Изменения counter, queue или routing обязаны
сохранять этот инвариант. Открытый destructor contract описан в
[B11](ISSUES.md#b11-cutexproxydestructor-бросает-из-noexcept-деструктора).

## I/O ownership

`include/ace/io.h` определяет общий ownership contract для `io::entity`,
`io::link`, query types и `io::guard`.

### Entity и link

- `io::entity` и `io::link` move-only и являются единственным владельцем FD;
- move construction переносит FD и инвалидирует source;
- move assignment сначала освобождает старый destination FD, затем принимает
  incoming ownership и перепривязывает guard к destination fields;
- self-move assignment является no-op и сохраняет ownership;
- state-machine transitions используют `consume()`/`extract()` и потребляют
  предыдущую entity;
- destructor guard закрывает оставшийся owned FD через fire-and-forget I/O path.

### Close contract

`entity::close()` немедленно инвалидирует entity и передаёт sole ownership FD
возвращаемому owning `io::close_query`. Если такой query уничтожен до submission,
его destructor всё равно инициирует ровно одно закрытие; после submission close
не отменяется. Повторный `entity::close()` возвращает ready no-op result.

Напрямую созданный `io::close_query(fd)` является non-owning: caller сохраняет
ownership до await/submission, а уничтожение не ожидавшегося query не закрывает
FD. Это намеренное различие зафиксировано в
[N4](ISSUES.md#n4-ownership-напрямую-созданного-и-entity-owned-close_query-различается).

### Binary read contract

- `io::read_query` и сетевые receive queries возвращают raw bytes и не добавляют
  NUL terminator;
- exact-size binary read не пишет за фактически прочитанным диапазоном;
- `transport_entity::recv(std::vector<T>&)` пишет только в существующие
  `size() * sizeof(T)` bytes, не в spare capacity;
- `transport_entity::recv(std::string&)` пишет только в существующие `size()`
  bytes;
- caller заранее изменяет logical size vector/string до требуемого writable
  диапазона; receive operation сама container не resize-ит.

`io::buffer` предоставляет chunked scatter-gather storage: `expand`, `append`,
`prepend`, `shape`, `assemble`, `disassemble`, `clone`, `clear`, `len` и `as<T>`.
`io::link` добавляет fire-and-forget `write`/`writeln`, async `read` и
`read_buf`.

## Сеть

`include/ace/net.h` строит move-consuming state machine поверх I/O entities:

```text
TCP socket -> socket_entity -> bind -> stream_mode_entity
                                      -> connect -> connection
                                      -> listen -> listener -> accept -> connection

UDP socket -> socket_entity -> bind -> net_interface
                                      -> connect -> connection
```

После каждого перехода исходная entity недействительна. Ошибочное потребление при
`bind`/`listen` и API retry отдельно отслеживается в
[B26](ISSUES.md#b26-ошибки-bindlisten-потребляют-исходную-entity).

Основные aliases и API:

| Тип | Назначение |
|-----|------------|
| `socket_tcp` | IPv4 TCP socket factory. |
| `socket_udp` | IPv4 UDP socket factory. |
| `socket_raw` | IPv4 raw socket factory. |
| `connection` | Connected IPv4 transport с `send`, `recv`, `recv_buf`. |
| `net_interface` | Indirect IPv4 transport с `sendto`, `recv`, `connect`. |
| `listener` | IPv4 listener с `accept`. |
| `connection_link` | Высокоуровневый `io::link` для connection. |

Aliases `socket_tcp_v6`, `socket_udp_v6` и `socket_raw_v6` существуют, но
**сейчас сломаны и не поддерживаются**: address storage, parsing и размеры всё ещё
IPv4-specific. IPv6 исправление явно отложено в
[B23](ISSUES.md#b23-ipv6-aliases-используют-ipv4-storage-и-размеры); до его
решения aliases нельзя считать рабочим API.

`connection::recv_buf()` возвращает eager `promise<io::input_t>` и требует
`co_await`. Известный EOF edge case накопленного `io::link::read_buf()` описан в
[B24](ISSUES.md#b24-iolinkread_buf-может-потерять-накопленные-данные-при-eof).

## Файлы и консоль

`include/ace/fs.h` определяет move-consuming `ace::fs::file`, async open queries и
`file_link`. `open_rdonly`, `open_wronly` и `open_rewrite` возвращают query;
`open_rewrite` использует truncate semantics.

`include/ace/console.h` определяет `ace::console::input`, `print` и `println`.
Свободные короткие aliases в namespace `ace` доступны только при корректном
include order, описанном в ограничениях.

## Dispatcher, runner и routing

### Dispatcher и runner

`dispatcher` из `include/ace/core/dispatcher.h` владеет runners. Главный поток
исполняет первый runner внутри `run()`, остальные работают в `std::jthread`.
Round-robin выбирает destination для новых задач.

`runner` из `include/ace/core/runner.h` содержит:

- local `reg_queue` для своих задач;
- MPSC insert queue для cross-thread задач;
- low-priority service queue для polling tasks;
- `attach`/`attach_front`, `reattach`/`reattach_front`, `yank`, `run` и load
  velocity;
- carrier path для typed async и automaton.

### Routing

`include/ace/core/traits/routing.h` определяет `runner_router_handle`,
control-block router interface и in-place `router_slot`. Конкретные routers
связывают runner с query, timeout, channel, cutex, compose и async handles.

`router_slot` хранит один router inline, поддерживает placement copy/move,
ownership steal, `release`, `reset` и `get`. Размер задаётся
`ACE_ROUTER_MEM_SIZE`.

### Сигналы

`include/ace/core/signal.h` определяет `signal_handler`, `termination_signal`,
`interruption_signal`, `sig_pipe_t` и `make_signal`. Service loop различает
`e_shutdown`, `e_idle` и `e_break`.

## io_uring и clock

### Kernel controller

`include/ace/services/kernelic.h` содержит thread-local `kernel_controller` для
каждого runner. Он готовит и отправляет socket, bind, connect, listen, accept,
send/receive, read/write, open/close, cancel и nop operations через `io_uring`.
Overflow SQEs буферизуются как `kernel_entity` и применяются после появления места
в ring. `kernel_observer` принимает CQE и возвращает waiter его runner.

Iovec storage использует общую arena. Большие physical chunks обслуживаются
transient path. Публичные `size_t` lengths пока могут сужаться во внутренних API;
это отслеживается в [B25](ISSUES.md#b25-io-lengths-сужаются-из-size_t-в-unsigned).

### Clock

`include/ace/services/clock.h` реализует hierarchical time wheel. `clock` -
thread-local service; `subscribe` вставляет timer record, `detach` отменяет его,
а `ping` продвигает wheel и reattach-ит истёкшие tasks. Timer records хранят
абсолютный deadline и `omni_node`. Cascade переносит записи с грубых уровней на
мелкие; release budget ограничивает объём работы одного ping.

`include/ace/futures/timeout.h` предоставляет relative `timeout` и absolute
`expire`. Cancellation удаляет timer и возвращает waiter в runner.

## Service, arena и tools

### Service

`include/ace/core/traits/service.h` содержит CRTP `service_traits` для фоновых
polling services. Поддерживаются thread-local и thread-shared spawn modes, sync
`ping() -> bool` и async `ping() -> promise<bool>` implementations, signal loop,
`touch`, `respawn` и `inspect`.

### Arena

`include/ace/core/arena.h` реализует единую thread-local arena для coroutine
frames, I/O/iovec и framework containers. Малые chunks идут в
`std::pmr::unsynchronized_pool_resource`, большие - в transient malloc path.
`arena_allocator<T>` адаптирует arena к стандартным containers.

Cross-thread release protocol:

- pooled chunk возвращается owner thread через MPSC channel;
- transient chunk освобождается сразу, а released bytes атомарно учитываются у
  owner;
- owner корректирует accounting при drain;
- runner threads должны завершиться до уничтожения owner storage.

Memory limit делится между runners. При breach arena либо использует transient
fallback, либо бросает `std::bad_alloc` согласно `_breach_memory_limit`.

### Tools

| Файл | Символы |
|------|---------|
| `include/ace/core/tools/queue.h` | `queue<T>`, `q_node<T>`, `slab_mempool<T>`. |
| `include/ace/core/tools/omniptr.h` | Type-agnostic pointer `omniptr<T...>`. |
| `include/ace/core/tools/id_alloc.h` | `id_allocator`, `async_id_allocator`. |
| `include/ace/core/tools/moving_average.h` | Fixed-window moving average. |
| `include/ace/core/tools/lifetime.h` | Debug lifetime tracer. |
| `include/ace/core/tools/macro.h` | Cache-line, router storage, diagnostics и inline macros. |

## Критические ограничения

1. Не использовать `&&` и `||` с не-`bool` типами. ACE перегружает logical
   operators для futures, поэтому mixed condition может попасть в compose
   overload. Использовать вложенные `if` и явное преобразование к `bool`.
2. `ace::async<T>` move-only. Учитывать rvalue semantics `co_await`.
3. Entity state machine потребляет objects через move; предыдущая entity после
   transition недействительна.
4. `connection::recv_buf()` возвращает eager `promise<io::input_t>` и требует
   `co_await`.
5. Socket entities не copyable; хранить через movable member или
   `std::optional`.
6. `schedule()` принимает `ace::task`; typed async требует `task_wrap` или
   соответствующий runner carrier path.
7. Capturing callable в `operator>>` принимает forwarded result по значению или
   `const&`, не по изменяемой `T&`.
8. Automaton создаётся suspended; значения продвигаются и потребляются через
   `ping()`, `join()` выполняет ping + cancel, specialized handle отменяет active
   automaton в destructor.
9. Coroutine lambdas запрещены из-за открытого
   [B13](ISSUES.md#b13-lambda-coroutine-повреждает-захваты-при-observe). Использовать
   named coroutine functions или helper methods с явными параметрами. Обычные
   некорутинные lambdas разрешены.
10. Короткие aliases futures и console под `#ifdef ACE_H` доступны только если
    `ace/ace.h` подключён раньше соответствующего header. Иначе использовать
    полные имена `ace::futures::*` и `ace::console::*`.
11. Сохранять arena cross-thread release protocol и lifetime owner storage.
12. IPv6 aliases не использовать до решения B23.

## Файловая карта

| Файл | Назначение |
|------|------------|
| `include/ace/ace.h` | Основной aggregate include и короткие aliases. |
| `include/ace/core/entry.h` | `co_main`, entry result, config initialization. |
| `include/ace/core/config.h` | Глобальная и compile-time конфигурация. |
| `include/ace/core/async.h` | `async`, `promise`, `automaton`, `task`, promise type и async router. |
| `include/ace/core/async_handle.h` | Spawn handles, join/ping handlers и routers. |
| `include/ace/core/control.h` | Control block, lifecycle и observer handle. |
| `include/ace/core/compose.h` | `or`, `and`, variadic compose и `operator>>`. |
| `include/ace/core/dispatcher.h` | Dispatcher и global scheduling API. |
| `include/ace/core/runner.h` | Runner queues, carrier, reattach и execution. |
| `include/ace/core/signal.h` | Signal handlers и signal pipe. |
| `include/ace/core/arena.h` | Shared arena и `arena_allocator`. |
| `include/ace/core/traits/future.h` | Future concepts и resume type traits. |
| `include/ace/core/traits/promise.h` | Coroutine rules и promise traits. |
| `include/ace/core/traits/routing.h` | Router interfaces и inline storage. |
| `include/ace/core/traits/service.h` | Background service CRTP. |
| `include/ace/io.h` | Queries, entity/link ownership, buffer, guard и hanged I/O. |
| `include/ace/net.h` | Socket state machine, transports и network queries. |
| `include/ace/fs.h` | File entity, open queries и file link. |
| `include/ace/console.h` | Async console input/output. |
| `include/ace/futures/channel.h` | Cooperative channels. |
| `include/ace/futures/cutex.h` | Cooperative mutex. |
| `include/ace/futures/timeout.h` | Relative и absolute timers. |
| `include/ace/futures/spawn.h` | Back-of-queue spawning. |
| `include/ace/futures/post.h` | Front-of-queue spawning. |
| `include/ace/futures/reattach.h` | Runner migration. |
| `include/ace/futures/roaming.h` | Migration policy. |
| `include/ace/futures/get_runner.h` | Current runner lookup. |
| `include/ace/futures/polling.h` | Low-priority task flag. |
| `include/ace/futures/backup.h` | Backup, insure и emergency callbacks. |
| `include/ace/services/kernelic.h` | `io_uring` controller и observers. |
| `include/ace/services/clock.h` | Hierarchical timer wheel. |
| `meson.build` | Build targets, source maps и test registration. |
| `discover_tests.py` | Source discovery и binary consistency verification. |

## Тесты, сборка и coverage

### Текущая карта

Test executable собирается из `tests/main.cpp`, `tests/environment.h` и **33
fixture source files**:

```text
arena_fixture.cpp              backup_fixture.cpp
base_fixture.cpp               channel_extra_fixture.cpp
channel_fixture.cpp            compose_extra_fixture.cpp
console_fixture.cpp            context_fixture.cpp
control_block_fixture.cpp      cross_mechanic_fixture.cpp
cutex_extra_fixture.cpp        cutex_fixture.cpp
dispatcher_fixture.cpp         fs_fixture.cpp
future_traits_fixture.cpp      get_runner_fixture.cpp
id_alloc_fixture.cpp           io_any_fixture.cpp
io_buffer_fixture.cpp          io_entity_fixture.cpp
io_hanged_fixture.cpp          moving_average_fixture.cpp
omniptr_fixture.cpp            promise_traits_fixture.cpp
queue_fixture.cpp              router_slot_fixture.cpp
runner_fixture.cpp             signal_fixture.cpp
socket_echo_fixture.cpp        spawn_extra_fixture.cpp
spawn_fixture.cpp              timer_fixture.cpp
yield_fixture.cpp
```

Fixture classes и helper coroutine functions объявляются в
`tests/environment.h`; каждый fixture source содержит относящиеся к нему
`TEST`/`TEST_F`. Текущая source inventory - **290 Google Tests**. Meson discover
mode регистрирует каждый GTest отдельным процессом с точным `--gtest_filter`.

Помимо source GTests, стандартная конфигурация регистрирует tooling tests:

- `discover_tests.unit` проверяет parser/discovery logic;
- `ace_tests.discovery_consistency` сравнивает source discovery со списком
  собранного GTest binary;
- `ace_entry.fallback` добавляется при включённом weak-entry mode.

### Текущий результат

- GCC: **291/292** registered tests проходят; единственный failure связан с
  некорректным automaton edge regression
  [B29](ISSUES.md#b29-некорректный-regression-блокирует-проверку-b16). Это не
  green suite.
- Clang: полноценная проверка заблокирована ODR/compiler-specific workaround
  [B27](ISSUES.md#b27-out-of-class-definitions-cutex-нарушают-odr-и-блокируют-clang-22)
  и неверным compiler detection
  [B28](ISSUES.md#b28-meson-принимает-argument-syntax-за-compiler-identity).

Не заявлять общий green status до решения этих записей. Meson запускает каждый
discovered GTest отдельным процессом; для проверки order dependencies дополнительно
использовать подходящие shuffle/repeated runs по `TESTING.md`.

### Coverage

Последний GCC gcov union от **2026-08-23** по уникальным строкам
`include/ace/**`: **2262/2398 = 94.33%**. Методика, команды и module gaps находятся
в `TESTING.md`; значение относится к фактически собранному GCC coverage union и
не означает успешный Clang suite.

### Benchmarks

Google Benchmark target `ace_benchmarks` включается `-Dbenchmarks=true`.
Актуальные scenarios, baseline protocol и inventory находятся в
`BENCHMARKS.md`; benchmark не заменяет correctness test.
