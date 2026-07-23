# ACE Framework — Complete LLM Navigation Index

## Оглавление

1. [Быстрый старт](#быстрый-старт)
2. [Корутины: async / promise / task](#корутины)
3. [Сеть: TCP / UDP / Raw](#сеть)
4. [Файлы и консоль](#файлы-и-консоль)
5. [Таймауты и таймеры](#таймауты)
6. [Комбинаторы: or / and / >>](#комбинаторы)
7. [spawn / post / schedule / reattach / roaming](#управление-задачами)
8. [Каналы и мьютексы](#каналы-и-мьютексы)
9. [Диспетчер и раннеры](#диспетчер-и-раннеры)
10. [Сигналы](#сигналы)
11. [Control block и async_handle](#control-block)
12. [I/O слой: io_query, io_entity, io_link](#io-слой)
13. [io_uring (kernel_controller)](#io_uring)
14. [Clock: иерархическое колесо времени](#clock)
15. [Router: маршрутизация futures](#router)
16. [Promise traits и память](#promise-traits)
17. [Tools: omniptr, queue, id_alloc, moving_average](#tools)
18. [Важные ограничения и паттерны](#ограничения)
19. [Файловая карта (полная)](#файловая-карта)
20. [Тесты](#тесты)

---

## Быстрый старт

```cpp
#include <ace/ace.h>

ace::task hello() { co_return; }

int main() {
    ace::schedule(hello());
    ace::run();  // блокирующий event loop
}
```

**Архитектура:** C++20 корутины + Linux `io_uring`. Один раннер на поток. Диспетчер распределяет задачи round-robin. Все операции асинхронные через `co_await`.

---

## Корутины

### async / promise / task

| Тип | Файл | Поведение |
|-----|------|-----------|
| `ace::async<T>` | `core/async.h:517` | Ленивая (`differed`). `initial_suspend() = suspend_always`. Запускается при `co_await` / `schedule`. |
| `ace::promise<T>` | `core/async.h:521` | Eager (`permanent`). `initial_suspend() = suspend_never`. Запускается сразу. |
| `ace::task` | `core/async.h:525` | `async<void>` — ленивая void-корутина. |
| `ace::suspend` | `core/async.h:549` | `std::suspend_always` — точка приостановки. |
| `ace::task_wrap(async&&)` | `core/async.h:527` | Оборачивает `async<T>` в `task`. Нужен для `schedule()` типизированной корутины. |
| `ace::omni_node` | `core/async.h:540` | Тип-agnostic указатель на pool-ноду (reg_queue или mpsc_queue). |
| `ace::omni_runner` | `core/async.h:543` | Тип-agnostic указатель на runner или его pool. |
| `ace::runner_router` | `core/async.h:546` | Абстрактный интерфейс для маршрутизации задач из раннера. |

**Ключевое различие promise vs async:**
```cpp
// promise — eager, начинает выполняться сразу при вызове
ace::promise<int> eager() {
    co_return 42;  // если нет co_await — выполнится до возврата из функции
}

// async — lazy, ждёт co_await или schedule
ace::async<int> lazy() {
    co_return 42;  // не выполнится, пока кто-то не сделает co_await
}

// task — lazy void, для schedule
ace::task work() {
    co_await eager();  // OK: co_await promise
    co_await lazy();   // OK: co_await async
    co_return;
}
```

### promise_type (внутреннее устройство)

`async<T,Rule>::promise_type` (`core/async.h:300`):
- `initial_suspend()` — `Rule::action()` (`suspend_always` / `suspend_never`). Устанавливает `_runner = get_current_pool()`.
- `final_suspend()` — `suspend_always` (static)
- `get_return_object()` → `async{coroutine_handle}`. Вызывает `setup_control_block()`.
- `return_value(v)` / `return_void()` — сохраняет значение, `status = e_finished`
- `unhandled_exception()` — `status = e_failed`, вывод в stderr
- `operator new(size_t)` — аллоцирует `control_block` перед promise
- `~promise_type()` — вызывает `control_block::disown(_block)` если блок есть
- **Поля:** `_runner_router` (router_slot_t), `_runner` (omni_runner), `_waiters`, `_self_router`, `_roaming`, `_polling`

### async:: методы

| Метод | Линия | Описание |
|-------|-------|----------|
| `async(async&&)` | `async.h:109` | Move-only, копирование удалено |
| `is_exist()` / `operator bool()` | `:145,150` | Активна ли корутина |
| `~async()` | `:156` | `release_waiters()`, cancel router если есть, `_coroutine.destroy()` |
| `is_resumable()` | `:193` | Нет busy future или busy future готов |
| `observe()` | `:207` | `control_block_handle` для join/cancel |
| `release_waiters()` | `:218` | Будит всех ожидающих через `push_node()` |
| `track()` | `:236` | `expected<size_t, string_view>` — trace ID |
| `await_ready()` | `:430` | Если `done()` — true. Иначе проверяет `_runner_router` и `is_resumable()` |
| `await_suspend(outer)` | `:450` | Копирует `_runner` из outer, переносит router через `operator<<` |
| `await_resume()` | `:464` | Возвращает `_return_value` или void |
| `awake(res*)` | `:487` | Возобновляет корутину из раннера |

### async_router (внутренний)

`async_router` (`async.h:251`) — наследник `control_router_handle`. Устанавливается в control_block при `setup_control_block()`.

| Метод | Линия | Описание |
|-------|-------|----------|
| `cancel()` | `:262` | Вызывает `_runner_router->cancel()` и `release()`, затем `status(e_detached)` |
| `redirect(void*)` | `:272` | Регистрирует waiter через `_waiters->push_node()` |

---

## Сеть

### Entity State Machine (потребление через move)

```
TCP:
socket_tcp → co_await → socket_entity → bind() → stream_mode_entity
                                              ├── connect() → connection (send/recv)
                                              └── listen() → listener → accept() → connection

UDP:
socket_udp → co_await → socket_entity → bind() → net_interface (sendto/recv)
                                          └── connect() → connection (send/recv — connected UDP)
```

Каждый шаг **потребляет** предыдущую сущность через move. После move старая сущность недействительна.

### Типы сокетов (`net.h:220-230`)

| Алиас | Определение |
|-------|-----------|
| `socket_tcp` | `socket<AF_INET, SOCK_STREAM, IPPROTO_TCP>` |
| `socket_tcp_v6` | TCP IPv6 |
| `socket_udp` | `socket<AF_INET, SOCK_DGRAM, IPPROTO_UDP>` |
| `socket_udp_v6` | UDP IPv6 |
| `socket_raw` | `socket<AF_INET, SOCK_RAW, IPPROTO_RAW>` |
| `connection` | `transport_entity<AF_INET, e_connected>` |
| `net_interface` | `transport_entity<AF_INET, e_indirect>` |
| `listener` | `listener_entity<AF_INET>` |

### socket_entity (`net.h:803`)

| Метод | Сигнатура | Constraints |
|-------|----------|-------------|
| `bind(sockaddr*, socklen_t)` | `→ bind_query` | — |
| `bind(in_addr_t, uint16_t)` | `→ bind_query` | `is_inet_domain` |
| `bind(string_view, uint16_t)` | `→ bind_query` | `is_inet_domain` |
| `connect(sockaddr*, socklen_t)` | `→ connect_query_t` | — |
| `connect(in_addr_t, uint16_t)` | `→ connect_query_t` | `is_inet_domain` |
| `connect(string_view, uint16_t)` | `→ connect_query_t` | `is_inet_domain` |

### stream_mode_entity (`net.h:727`)

| Метод | Сигнатура |
|-------|----------|
| `listen(int backlog=0)` | `→ listen_query` |
| `connect(...)` | `→ connect_query_t` (3 overloads) |

### connection = transport_entity\<AF_INET, e_connected\> (`net.h:387`)

| Метод | Возврат | Описание |
|-------|---------|----------|
| `send(const void*, size_t, flags=0)` | `send_query` | `co_await` → `int` (bytes sent) |
| `send(string_view, flags=0)` | `send_query` | |
| `send(vector<T>&, flags=0)` | `send_query` | |
| `send(array<T,N>&, flags=0)` | `send_query` | |
| `send(span<T>&, flags=0)` | `send_query` | |
| `send(io::buffer&, flags=0)` | `sendmsg_query` | scatter-gather send |
| `recv(void*, size_t, flags=0)` | `recv_query` | `co_await` → `int` (bytes recv) |
| `recv(vector<T>&, flags=0)` | `recv_query` | |
| `recv(string&, flags=0)` | `recv_query` | |
| `recv(array<T,N>&, flags=0)` | `recv_query` | |
| `recv(span<T>&, flags=0)` | `recv_query` | |
| `recv(io::buffer&, flags=0)` | `recvmsg_query` | scatter-gather recv |
| `recv_buf(flags=0)` | `promise<io::input_t>` | eager, читает в `io::buffer` |

### net_interface = transport_entity\<AF_INET, e_indirect\> (`net.h:387`)

| Метод | Возврат | Constraints |
|-------|---------|-------------|
| `sendto(void*, size_t, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `sendto(string_view, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `sendto(vector<T>&, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `sendto(string&, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `sendto(array<T,N>&, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `sendto(span<T>&, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `recv(...)` — те же 6 overloads что и `connection::recv` | `recv_query` | no constraint |
| `connect(...)` — 3 overloads | `connect_query_t` | `e_indirect` |

### listener (`net.h:656`)

| Метод | Возврат |
|-------|---------|
| `accept()` | `accept_query` → `co_await` → `connection` |
| `accept(sockaddr*, socklen_t*)` | `accept_query` |
| `accept(in_addr_t, uint16_t)` | `accept_query` |
| `accept(string_view, uint16_t)` | `accept_query` |

### connection_link (`net.h:289`)

Высокоуровневая обёртка над `connection`:
- `write(data)` / `writeln(fmt, args...)` — fire-and-forget (не требует `co_await`)
- `read(buf, len)` — `async<int>`, требует `co_await`
- `read_buf()` — `async<io::input_t>`

### TCP паттерн (клиент)

```cpp
ace::task tcp_client(std::string host, uint16_t port) {
    auto sock = co_await ace::net::socket_tcp();
    if (not sock) co_return;
    auto stream = co_await sock.bind("0.0.0.0", 0);
    if (not stream) co_return;
    auto conn = co_await stream.connect(host, port);
    if (not conn) co_return;
    co_await conn.send("hello");
    auto result = co_await conn.recv_buf();
    if (result) { auto body = result.value().as<std::string>(); }
}
```

### TCP паттерн (сервер)

```cpp
ace::task tcp_server() {
    auto sock = co_await ace::net::socket_tcp();
    auto stream  = co_await sock.bind("0.0.0.0", 8080);
    auto listener = co_await stream.listen(128);
    while (true) {
        auto conn = co_await listener.accept();
        auto data = co_await conn.recv_buf();
    }
}
```

### UDP паттерн

```cpp
ace::task udp_client() {
    auto sock = co_await ace::net::socket_udp();
    auto udp = co_await sock.bind("0.0.0.0", 0);
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(2123);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);
    co_await udp.sendto("ping", 4, 0,
        reinterpret_cast<sockaddr*>(&server), sizeof(server));
    char buf[1500];
    int n = co_await udp.recv(buf, sizeof(buf));
}
```

---

## Файлы и консоль

### ace::console (`console.h`)

| Метод | Сигнатура |
|-------|----------|
| `input()` | `static async<io::input_t>` — асинхронный stdin |
| `println(fmt, args...)` | `static void` — format + newline |
| `println(string_view)` | `static void` |
| `println()` | `static void` — пустая строка |
| `print(fmt, args...)` | `static void` — format без newline |
| `print(string_view)` | `static void` |

### ace::fs (`fs.h`)

| Тип | Описание |
|-----|----------|
| `ace::fs::file(path)` | `io_entity` для файлов |
| `file.open(flags, mode)` | `→ open_query → co_await → file_link` |
| `file.open_rdonly()` | Открыть только на чтение |
| `file.open_wronly()` | Открыть только на запись |
| `file.open_rewrite()` | Открыть на перезапись |
| `ace::fs::file_link` | `io_link` для открытого файла — `write()` / `read()` / `read_buf()` |

---

## Таймауты

### ace::futures::timeout / expire (`futures/timeout.h`)

```cpp
co_await ace::futures::timeout(500ms);     // относительный
co_await ace::futures::timeout(5s);

auto deadline = clock::current_time() + 2s;
co_await ace::futures::expire(deadline);   // абсолютный
```

**Внутреннее устройство:** `timeout_router` (`timeout.h:122`) помещает задачу в `clock::subscribe()` (иерархическое колесо времени). Когда время истекает, `clock::ping()` возвращает задачу через `runner::reattach()`. При отмене `cancel()` вызывает `clock::detach()` и `runner::reattach()` ноды.

### Гонка recv с таймаутом

```cpp
auto result = co_await (
    conn.recv(buf, sizeof(buf)) or
    ace::futures::timeout(5s)
);
// result: std::variant<int,int>
// index 0 = recv выиграл, index 1 = таймаут выиграл
```

---

## Комбинаторы

### `operator or` (гонка) — `core/compose.h:700`

| Операнды | Результат |
|---------|-----------|
| `void or void` | `int` (0 = left won, 1 = right won) |
| `T or void` | `std::optional<T>` |
| `void or T` | `std::optional<T>` |
| `T or U` | `std::variant<T,U>` |

### `operator and` (параллельное ожидание) — `core/compose.h:724`

| Операнды | Результат |
|---------|-----------|
| `void and void` | `void` |
| `T and void` | `T` |
| `void and T` | `T` |
| `T and U` | `std::tuple<T,U>` |

### `operator >>` (монадический пайп) — `core/compose.h:884`

```cpp
co_await (fetch_user() >> process_user);
// co_await fetch_user(), результат передаётся в process_user, co_await process_user
```

### Цепочки (variadic)

`or_await_composed<F...>` и `and_await_composed<F...>` позволяют цепочки из 3+ futures:
```cpp
co_await (a and b and c);    // and_await_composed → tuple<A,B,C>
co_await (a or b or c);      // or_await_composed → variant / optional / int
```

---

## Управление задачами

### schedule / run / empty / reload (`core/dispatcher.h`)

| Функция | Назначение |
|---------|-----------|
| `ace::schedule(task&&, runner* = nullptr)` | Поставить задачу в event loop. `runner*` — на конкретный раннер. |
| `ace::run()` | Запустить event loop. БЛОКИРУЕТ вызывающий поток. |
| `ace::empty()` | `bool` — все раннеры idle? |
| `ace::reload()` | Переконфигурировать количество раннеров. |
| `ace::interrupt()` | Послать сигнал `e_break` |
| `ace::terminate()` | Послать сигнал `e_shutdown` |
| `ace::reset_signal()` | Слить signal pipe |

Конфигурация: `ace::cfg::g_config._runners_amount = 4; ace::reload();`

### spawn (`futures/spawn.h`)

```cpp
auto handle = co_await ace::spawn(worker());
// handle: async_handle
co_await handle.join();  // дождаться завершения
handle.cancel();         // отменить
handle.done();           // проверить завершение
```

**`co_await spawn()` НЕ суспендит вызывающего** — возвращает управление немедленно. Задача запускается на том же раннере через `attach()`. `_roaming = false` (привязана к раннеру).

### post (`futures/post.h`)

Как `spawn`, но задача помещается в НАЧАЛО очереди раннера через `attach_front()` (приоритет). `_roaming = false`.

### roaming / reattach / get_runner

| Функция | Файл | Назначение |
|---------|------|-----------|
| `co_await ace::roaming(bool)` | `futures/roaming.h` | Разрешить/запретить миграцию между раннерами |
| `co_await ace::reattach(runner*)` | `futures/reattach.h` | Мигрировать на конкретный раннер |
| `co_await ace::get_runner{}` | `futures/get_runner.h` | Получить `runner*` текущего раннера |

### polling (`futures/polling.h`)

```cpp
co_await ace::polling(true);  // пометить задачу как низкоприоритетную (vortex)
```

---

## Каналы и мьютексы

### channel\<T\> (`futures/channel.h`)

MPMC (multi-producer/multi-consumer) канал на lock-free очереди.

| Метод | Сигнатура | Описание |
|-------|----------|----------|
| `push(T&)` / `push(T&&)` | `bool` | Синхронный push. `false` если буфер полон. |
| `operator<<(T)` | `channel&` | Сахар для `push()` |
| `pull()` | `pull_impl` | Awaitable. `co_await` возвращает `T`. |
| `pending_push(T)` | `promise<>` | Асинхронный push — ждёт пока появится место |

```cpp
ace::futures::tunnel::dyn::bus<int> ch;
ch.push(42);
ch << 99;
int val = co_await ch.pull();
```

**Фоновый воркер:**
```cpp
ace::task bg_worker(auto& ch) {
    while (true) {
        auto msg = co_await ch.pull();
        // обработать...
    }
}
// запуск:
co_await ace::spawn(bg_worker(ch));  // работает вечно
```

### cutex — cooperative mutex (`futures/cutex.h`)

```cpp
ace::cutex mtx;

ace::task critical_section() {
    volatile auto guard = ace::guard(mtx);
    co_await guard.capture();
    // --- критическая секция ---
    guard.sync();  // разблокировка (также авто-вызов в ~proxy())
    co_return;
}
```

**Внутреннее устройство:** `cutex_router` (`cutex.h:265`) помещает ожидающие задачи в `roaming_mpsc_queue`. `cancel()` — no-op (задачи остаются в очереди, RAII proxy гарантирует `sync()`). При освобождении `sync()` пробуждает следующего ожидающего через `notify()`.

---

## Диспетчер и раннеры

### dispatcher (`core/dispatcher.h:69`)

Синглтон, управляет N раннерами. Главный поток выполняет `runner[0]` внутри `run()`. Рабочие потоки (1..N-1) — `std::jthread`, вызывают `worker_round()` в цикле.

- `worker_round()` — ~1ms работы, затем sleep 1ms если idle
- `round_robin(task&&)` — распределяет задачи по кругу
- `fetch_config()` — перечитывает `_runners_amount` при `reload()`

### runner (`core/runner.h:51`)

Per-thread исполнитель. Три очереди:
- `_pool` — lock-free MPSC для локальных задач
- `_insert_pool` — lock-free MPSC для кросс-поточных вставок
- `_vortex_pool` — низкоприоритетные (polling) задачи

| Метод | Линия | Назначение |
|-------|-------|-----------|
| `attach(async&&)` | `:296` | Добавить задачу в очередь |
| `attach_front(async&&)` | `:307` | Добавить в начало очереди |
| `yank()` | `:323` | Обработать одну задачу |
| `yank_vortex()` | `:373` | Обработать vortex-задачу |
| `run()` | `:411` | Обработать до 128 задач за раз |
| `reattach(omni_node&, runner)` | `:276` | Вернуть ноду владеющему раннеру |
| `reattach_front(omni_node&, runner)` | `:285` | Вернуть ноду в начало очереди |
| `fetch_task_node()` | `:428` | Извлечь ноду из `_pool` или `_insert_pool` |
| `velocity()` | `:317` | `double` — скользящее среднее загрузки |

**Ключевое изменение refactor:** все `reattach`/`reattach_front` методы принимают `omni_node` вместо подтипов. `yank()` использует `omni_node` через `fetch_task_node()`. Поле кросс-поточных вставок переименовано `_interthread_pool` → `_insert_pool`.

---

## Сигналы

### signal_handler (`core/signal.h:38`)

| Тип | Описание |
|-----|----------|
| `signal_handler` | Абстрактный: `virtual async<signal_trivial_orders> action() = 0` |
| `termination_signal` | `action()` → `e_shutdown` |
| `interruption_signal` | `action()` → `e_break` |
| `sig_pipe_t` | `mpsc_queue<unique_ptr<signal_handler>>` — очередь сигналов |

### signal_trivial_orders (`core/signal.h:28`)

```cpp
enum signal_trivial_orders { e_shutdown, e_idle, e_break };
```

---

## Control block

### control_block (`core/control.h:68`)

Intrusive reference-counted блок управления временем жизни корутины.

| Поле/Метод | Описание |
|-----------|----------|
| `_weak_refcount` | Счётчик наблюдателей (control_block_handle) |
| `_strong_refcount` | Счётчик владельцев (coroutine frame) |
| `_control_router` | `control_router_handle*` — указатель на router для cancel/redirect |
| `_frame_size` | Размер фрейма (0 = destroyed) |
| `_status` | `promise_lifecycle` |
| `watch(void*)` | Управление weak refcount |
| `unwatch(void*)` | Декремент weak refcount |
| `disown(void*)` | Декремент strong refcount, освобождение если оба 0 |

### promise_lifecycle (`core/control.h:47`)

```cpp
enum promise_lifecycle {
    e_inited, e_executed, e_executed_with_value,
    e_finished, e_failed, e_detached
};
```

### control_block_handle (`core/control.h:154`)

Copyable внешний handle для наблюдения за корутиной.

| Метод | Описание |
|-------|----------|
| `cancel()` | Отменить корутину (останавливает router, ставит `e_detached`) |
| `done()` | `bool` — `_frame_size == 0`? |
| `finished()` | `bool` — `_status == e_finished`? |
| `is_idle()` | `bool` — корутина не выполняется? |
| `forward(void*)` | Передать waiter'а через `_control_router->redirect()` |

### async_handle (`core/async_handle.h:100`)

Handle для spawn-нутых задач. Наследует `control_block_handle` (через `join_handler`).

| Метод | Описание |
|-------|----------|
| `join()` | `join_handler&` — `co_await` для ожидания завершения |
| `done()` | `bool` — завершена? |
| `cancel()` | Отменить |

---

## I/O слой

### io::query\<Q\> (`io.h:244`)

CRTP база для io_uring операций. `await_suspend()` submit-ит SQE через `kernel_controller`.

| Метод | Линия | Описание |
|-------|-------|----------|
| `query_router` | `:262` | Router: сохраняет `omni_node` в `_waiter`, пересылает через `redirect()` |
| `await_ready()` | `:288` | всегда `false` |
| `await_suspend(auto)` | `:290` | Submit SQE, установка query_router |
| `on_result(int)` | `:305` | Сохраняет `_res`, делает `runner::reattach(_waiter)` |

**Поля:** `_waiter` (omni_node), `_res` (int, default INT_MIN), `_fd` (int), `_is_silent` (bool)

### read_query / write_query / close_query (`io.h:321,354,382`)

| Тип | Конструктор |
|-----|------------|
| `read_query` | `(int fd, void* buf, unsigned nbytes, uint64_t offset=0)` |
| `write_query` | `(int fd, const void* buf, unsigned nbytes, uint64_t offset=0)` |
| `close_query` | `(int fd)` |

### io::entity\<E\> (`io.h:972`)

CRTP база для владельцев файловых дескрипторов.

| Метод | Описание |
|-------|----------|
| `consume(entity_t&)` | `static` — извлечь FD и создать новый entity |
| `extract()` | `tuple{_fd, _is_closed}` — украсть FD |
| `close()` | `close_query` — асинхронное закрытие |
| `is_closed()` | `bool` |
| `operator bool()` | `_fd > -1` — валидный FD? |

Поля: `_fd` (int), `_is_closed` (bool), `_guard` (io::guard).

### io::guard (`io.h:931`)

RAII: асинхронно закрывает FD в деструкторе. Использует `io::hanged::command` для fire-and-forget close, либо `schedule(pending_close)` если нет io_uring.

### io::link (`io.h:1053`)

Высокоуровневый I/O поверх `io::entity`.

| Метод | Сигнатура |
|-------|----------|
| `write(data)` | `void` — 6 перегрузок (string_view, void*, vector, array, span…) |
| `writeln(fmt, args...)` | `void` — format + newline |
| `read(buf, len)` | `async<int>` — 6 перегрузок |
| `read_buf()` | `async<io::input_t>` |

### io::buffer (`io.h:447`)

Scatter-gather буфер на iovec'ах. `expand(len)` — выделить память для чтения, `append(data)` — добавить данные, `assemble()` → `msghdr*`, `as<T>()` — конвертировать, `clone()` — копия.

### io::hanged (`io.h:885`)

Пул fire-and-forget I/O команд для использования вне корутин (например, в деструкторах `io::guard`).

---

## io_uring

### kernel_controller (`services/kernelic.h:87`)

Thread-local vortex. Каждый раннер имеет свой экземпляр с собственным `io_uring` ring (4096 entries).

| Метод | io_uring обёртка |
|-------|-----------------|
| `socket(obs, domain, type, proto, flags)` | `io_uring_prep_socket` |
| `bind(obs, fd, addr, addrlen)` | `io_uring_prep_bind` |
| `connect(obs, fd, addr, addrlen)` | `io_uring_prep_connect` |
| `listen(obs, fd, backlog)` | `io_uring_prep_listen` |
| `accept(obs, fd, addr, addrlen, flags)` | `io_uring_prep_accept` |
| `send(obs, fd, buf, len, flags)` | `io_uring_prep_send` |
| `sendto(obs, fd, buf, len, flags, addr, addrlen)` | `io_uring_prep_sendto` |
| `recv(obs, fd, buf, len, flags)` | `io_uring_prep_recv` |
| `read(obs, fd, buf, nbytes, offset)` | `io_uring_prep_read` |
| `write(obs, fd, buf, nbytes, offset)` | `io_uring_prep_write` |
| `writev(obs, fd, vec, len, offset, flags)` | `io_uring_prep_writev2` |
| `open(obs, path, flags, mode)` | `io_uring_prep_open` |
| `close(obs, fd)` | `io_uring_prep_close` |
| `cancel(obs, flags)` | `io_uring_prep_cancel` |
| `cancel_fd(obs, fd, flags)` | `io_uring_prep_cancel_fd` |
| `nop(obs)` | `io_uring_prep_nop` |
| `ping()` | Дренирует SQEs, обрабатывает CQEs |

**iovec аллокатор:** `iovec_allocate(n)` / `iovec_deallocate(iov)` / `iovec_pool_allocate(n)` / `iovec_pool_deallocate(iov, n)` — выделение/освобождение iovec структур из slab-пула.

**kernel_observer** (`kernelic.h:52`): полиморфный обработчик CQE. Поля: `_runner_identity` (runner_pool_t*), `_on_cancel` (bool), `_multishot` (bool). `on_result(int res)` вызывается при получении CQE.

---

## Clock

### clock / multi_dial (`services/clock.h`)

Иерархическое колесо времени с O(1) вставкой и освобождением.

| Компонент | Описание |
|-----------|----------|
| `clock` | Thread-local vortex. `ping()` освобождает истекшие таймеры. |
| `multi_dial` | Полное колесо: 5 уровней (1ms → 256ms → 65s → 4.6h → 49d) |
| `dial` | Один уровень колеса |
| `time_slot` | Один слот в уровне |
| `clock_record` | Запись таймера: `duration_t _duration` + `omni_node _context` |

| Метод clock | Назначение |
|------------|-----------|
| `current_time()` | Кешированный `steady_clock::now()` |
| `subscribe(omni_node, duration)` | Подписать задачу на таймер |
| `detach(node*)` | Отменить таймер (возвращает ноду в runner) |
| `ping()` | Освободить истекшие таймеры |

**Важно:** `clock_record::_context` — `omni_node` (не `task`). При отмене таймера `detach_record()` делает `runner::reattach()` ноды перед удалением из колеса.

---

## Router

### router_slot (`core/traits/routing.h:128`)

In-place storage для одного router'а (размер `ACE_ROUTER_MEM_SIZE` = cache_line - bus_size).

| Метод | Назначение |
|-------|-----------|
| `operator=(const router_t&)` | Placement new копия |
| `operator=(router_t&&)` | Placement new move |
| `operator<<(carry_t&)` | Украсть router из другого слота через memcpy |
| `release()` | Уничтожить router (вызов виртуального деструктора) |
| `reset()` | Обнулить указатель без вызова деструктора |
| `get()` | `router_handle_t*` |
| `operator bool()` | Занят ли слот |

### runner_router_handle (`routing.h:50`)

Абстрактный интерфейс для маршрутизации задач из раннера:
- `redirect(omni_node node)` — переслать ноду в хранилище future
- `cancel()` — отменить (default: no-op)

### control_router_handle (`routing.h:87`)

Абстрактный интерфейс для control-block join/cancel:
- `redirect(void* waiter)` — pure virtual, пробудить ожидающего
- `cancel()` — pure virtual, отменить

### Конкретные router'ы

| Router | Файл | `redirect()` | `cancel()` |
|--------|------|-------------|------------|
| `query_router` | `io.h:262` | `_query->_waiter = node` | `kernel_controller::cancel()` |
| `timeout_router` | `timeout.h:122` | `clock::subscribe(node, dur)` | `clock::detach()` + `runner::reattach()` |
| `cutex_router` | `cutex.h:265` | `_cutex->_waiters.push_node(node)` | no-op |
| `channel_router` | `channel.h:314` | `_channel->push_node(node)` | reattach all + pop |
| `reattach_router` | `reattach.h:83` | Обновляет `_runner` + `reattach(node)` | — |
| `or_await_router` | `compose.h:480` | Сохраняет в `_waiter` | — |
| `and_await_router` | `compose.h:529` | Сохраняет в `_waiter` | — |
| `join_handler_router` | `async_handle.h:137` | `_handle.forward(node)` | — |
| `async_router` | `async.h:251` | `_waiters->push_node(waiter)` | cancel router + `e_detached` |

---

## Promise traits

### permanent / automaton / differed (`core/traits/promise.h:48,60,72`)

| Tag | `action()` | Поведение |
|-----|-----------|----------|
| `permanent` | `suspend_never` | Eager — корутина стартует сразу |
| `automaton` | `suspend_never` | Eager, без cancel/join поддержки (без control_block) |
| `differed` | `suspend_always` | Lazy — ждёт `co_await` |

### promise_traits\<D, T\> (`core/traits/promise.h:196`)

Основной promise type. Наследует `promise_return_traits<D,T>`.

| Метод | Назначение |
|-------|-----------|
| `await_transform(futureT&)` | 4 перегрузки: router-based (clears `_busy_future`) и busy-polling (sets `_busy_future`) |
| `operator new(size_t)` | Аллокация `control_block` + promise |
| `operator delete(void*)` | disown control block |
| `setup_trace()` | Выделить trace ID |
| **Поля:** `_busy_future`, `_block`, `_trace_id` |

---

## Tools

### omniptr\<T, Ts...\> (`core/tools/omniptr.h:10`)

Тип-agnostic указатель для безопасного хранения одного из нескольких типов указателей.

| Метод | Назначение |
|-------|-----------|
| `omniptr(void*)` | Неявное конструирование из void* |
| `omniptr(T*)` | Конструирование из конкретного типа |
| `as<T>()` | `static_cast<T*>(_ptr)` |
| `operator T*()` | Неявное приведение |
| `operator->()` | Доступ через первый шаблонный параметр |
| `reset()` | Обнулить |

Используется как `omni_node` (reg_queue::node_t \| mpsc_queue::node_t) и `omni_runner` (runner \| runner_pool_t).

### queue\<T\> (`core/tools/queue.h:131`)

Интрузивная двусвязная FIFO очередь на `slab_mempool`.

| Метод | Назначение |
|-------|-----------|
| `enqueue(T&&)` | Добавить в конец |
| `dequeue()` | Извлечь из начала (возвращает T) |
| `pop()` | Извлечь узел без разрушения |
| `empty()` | `bool` |
| `remove_node(node*)` | Удалить конкретный узел |

### slab_mempool\<T\> (`core/tools/queue.h:63`)

Slab-аллокатор. `alloc()` / `free(node*)`. Чанки по 1024 узла.

### id_allocator / async_id_allocator (`core/tools/id_alloc.h`)

Lock-free аллокатор уникальных ID. `id_alloc()` выделяет, `id_free()` возвращает в пул.

### moving_average (`core/tools/moving_average.h`)

Скользящее среднее с окном 4. `add(val)` — добавить значение, `value()` — текущее среднее.

### lifetime (`core/tools/lifetime.h`)

RAII debug tracer: логирует конструирование/разрушение. `track()` / `untrack()` — глобальное вкл/выкл.

---

## Ограничения

1. **НЕ использовать `&&` и `||` с не-bool типами** — ACE переопределяет их для futures через `operator&&`/`operator||` из `compose.h`. Любое выражение вида `optional && bool` или `bool && function` будет поймано шаблонными перегрузками ACE. Решение: вложенные `if`.

2. **`ace::async<T>` — move-only**, копирование удалено.

3. **Entity state machine потребляет через move** — после каждого шага старая сущность недействительна.

4. **`connection::recv_buf()` возвращает `promise<expected<buffer, int>>`** — eager корутина, требующая `co_await`.

5. **Хранение сокетов в классах** — OK через `std::optional<connection>` или как член класса. `io::entity` не copyable, но movable.

6. **`co_await` rvalue async** — `operator co_await()` работает только с rvalue для move-only типов.

7. **`schedule()` требует `ace::task`** — для типизированных корутин используй `task_wrap()`.

---

## Файловая карта

| Файл | Что содержит |
|------|-------------|
| `ace.h` | Master include: async, dispatcher, compose, spawn, post, reattach, get_runner, roaming |
| `core/async.h` | `async<T>`, `promise<T>`, `task`, `task_wrap`, `suspend`, promise_type, async_router, omni_node/omni_runner/runner_router aliases |
| `core/async_handle.h` | `async_handle` (join/cancel/done), `join_handler` |
| `core/compose.h` | `or_await`, `and_await`, `or/and_await_composed`, `operator or/and/>>` |
| `core/control.h` | `control_block`, `control_block_handle`, `promise_lifecycle` |
| `core/dispatcher.h` | `dispatcher`, `schedule`, `run`, `empty`, `reload`, `interrupt`, `terminate` |
| `core/runner.h` | `runner` (per-thread), `attach`, `reattach`, `yank`, `run`, `velocity` |
| `core/signal.h` | `signal_handler`, `sig_pipe_t`, `termination_signal`, `interruption_signal` |
| `core/traits/future.h` | `future_traits`, `busy_future_traits`, concepts (`is_future`, `is_awaitable`), type traits |
| `core/traits/promise.h` | `permanent`, `automaton`, `differed`, `promise_traits`, `promise_return_traits` |
| `core/traits/routing.h` | `runner_router_handle`, `control_router_handle`, `router_slot` |
| `core/traits/vortex.h` | `vortex_traits` CRTP для фоновых сервисов |
| `core/tools/omniptr.h` | `omniptr<T, Ts...>` — тип-agnostic указатель |
| `core/tools/queue.h` | `queue<T>`, `q_node<T>`, `slab_mempool<T>` |
| `core/tools/id_alloc.h` | `id_allocator`, `async_id_allocator` |
| `core/tools/macro.h` | `ACE_CACHE_LINE_SIZE`, `ACE_ROUTER_MEM_SIZE`, `ACE_AWAIT_NODISCARD` |
| `core/tools/moving_average.h` | `moving_average` (sliding window) |
| `core/tools/lifetime.h` | `lifetime` (RAII debug tracer) |
| `net.h` | Все TCP/UDP типы: `socket`, `socket_entity`, `stream_mode_entity`, `listener_entity`, `transport_entity`, `connection_link`, все query-типы |
| `io.h` | `io::query`, `io::entity`, `io::link`, `io::guard`, `io::hanged`, `io::buffer`, `io::any`, read/write/close_query |
| `console.h` | `ace::console::input()`, `println()`, `print()` |
| `fs.h` | `ace::fs::file`, `file_link` |
| `futures/channel.h` | `channel<T>` (MPMC), `channel_st<T>` (single-thread), `push`, `pull`, `pending_push` |
| `futures/cutex.h` | `cutex` (cooperative mutex), `guard`, `cutex_router` |
| `futures/timeout.h` | `timeout(duration)`, `expire(deadline)`, `timeout_router` |
| `futures/spawn.h` | `spawn(task)` — параллельный запуск |
| `futures/post.h` | `post(task)` — приоритетный запуск |
| `futures/reattach.h` | `reattach(runner*)` — миграция корутины |
| `futures/roaming.h` | `roaming(bool)` — флаг миграции |
| `futures/get_runner.h` | `get_runner` — текущий раннер |
| `futures/polling.h` | `polling(bool)` — флаг низкого приоритета |
| `services/kernelic.h` | `kernel_controller` (io_uring vortex), `kernel_observer`, все `io_uring_prep_*` |
| `services/clock.h` | `clock` vortex, `multi_dial` (временное колесо), `clock::subscribe()`, `clock::ping()` |

---

## Тесты

Тесты находятся в `tests/`. Используют Google Test с fixture-based архитектурой.

| Файл | Назначение |
|------|-----------|
| `tests/main.cpp` | GTest main |
| `tests/units.h` | Include-хаб: `ace/ace.h`, `gtest/gtest.h`, namespace `tool`, `#include "fixtures.h"` |
| `tests/fixtures.h` | Все fixture-классы с хелпер-тасками |
| `tests/tests.cpp` | `TEST_F` тесты |
| `tests/environment.h` | `#include "units.h"` |

### Fixture-классы

| Fixture | Наследует | SetUp/TearDown | Тесты |
|---------|----------|----------------|-------|
| `BaseFixture` | `::testing::Test` | — | Базовый: `once_suspend`, `channel_fetcher<T>`, `sleeper`, `fancy`, `fetch<T>(ch)` |
| `ContextFixture` | `BaseFixture` | — | 5 тестов: coroutine lifecycle |
| `ChannelFixture` | `BaseFixture` | — | 1 тест: channel send/receive |
| `TimerFixture` | `BaseFixture` | — | 5 тестов: timer, expire, or, and |
| `TimerParallelFixture` | `BaseFixture` | `_runners=4` → reset | 1 тест: parallel timers |
| `CutexFixture` | `BaseFixture` | `configure_runners(n)`, `TearDown` reset | 4 теста: race + cancel |
| `SpawnFixture` | `BaseFixture` | — | 6 тестов: spawn, post, cancel, join, compose |
| `SocketEchoFixture` | `BaseFixture` | `TearDown` reset_signal | 2 теста: TCP echo |
| `FsFixture` | `BaseFixture` | — | 1 тест: filesystem |

### Добавление новых тестов

1. Добавить хелпер-таски как методы fixture-класса
2. Написать `TEST_F(FixtureName, test_name) { ... }`
3. Общие `fetch<T>(ch)`, `channel_fetcher`, `sleeper` — уже в `BaseFixture`
