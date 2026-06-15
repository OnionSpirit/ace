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
15. [Conductor: проводники futures](#conductor)
16. [Promise traits и память](#promise-traits)
17. [Tools: queue, id_alloc, moving_average](#tools)
18. [Важные ограничения и паттерны](#ограничения)
19. [Файловая карта (полная)](#файловая-карта)
20. [Исправления (custom patches)](#исправления)

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
| `ace::async<T>` | `core/async.h:538` | Ленивая (`differed`). `initial_suspend() = suspend_always`. Запускается при `co_await` / `schedule`. |
| `ace::promise<T>` | `core/async.h:542` | Eager (`permanent`). `initial_suspend() = suspend_never`. Запускается сразу. |
| `ace::task` | `core/async.h:545` | `async<void>` — ленивая void-корутина. |
| `ace::suspend` | `core/async.h:561` | `std::suspend_always` — точка приостановки. |
| `ace::task_wrap(async&&)` | `core/async.h:549` | Оборачивает `async<T>` в `task`. Нужен для `schedule()` типизированной корутины. |

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

`async<T,Rule>::promise_type` (`core/async.h:319`):
- `initial_suspend()` — `Rule::action()` (`suspend_always` / `suspend_never`)
- `final_suspend()` — `suspend_always`, декрементит strong refcount
- `get_return_object()` → `async{coroutine_handle}`
- `return_value(v)` / `return_void()` — сохраняет значение, `status = e_finished`
- `yield_value(v)` — сохраняет значение, `status = e_executed_with_value`
- `unhandled_exception()` — `status = e_failed`, вывод в stderr
- `operator new(size_t)` — аллоцирует `control_block` перед promise
- Поля: `_runner_conductor`, `_runner`, `_waiters`, `_self_conductor`, `_roaming`, `_polling`

### async:: методы

| Метод | Линия | Описание |
|-------|-------|----------|
| `async(async&&)` | `async.h:143` | Move-only, копирование удалено |
| `is_exist()` / `operator bool()` | `:179,184` | Активна ли корутина |
| `observe()` | `:227` | `control_block_handle` для join/cancel |
| `release_waiters()` | `:238` | Будит всех ожидающих |
| `track()` | `:255` | `expected<size_t, string_view>` — trace ID |
| `awake(res*)` | `:508` | Возобновляет корутину из раннера |

---

## Сеть

### Entity State Machine (потребление через move)

```
TCP:
io_socket_tcp → co_await → io_mapping_entity → bind() → io_stream_mode_entity
                                                         ├── connect() → io_connection (send/recv)
                                                         └── listen() → io_listener → accept() → io_connection

UDP:
io_socket_udp → co_await → io_mapping_entity → bind() → io_net_interface (sendto/recv)
                                                    └── connect() → io_connection (send/recv — connected UDP)
```

Каждый шаг **потребляет** предыдущую сущность через move. После move старая сущность недействительна.

### Типы сокетов (`net.h:220-230`)

| Алиас | Определение |
|-------|-----------|
| `io_socket_tcp` | `io_socket<AF_INET, SOCK_STREAM, IPPROTO_TCP>` |
| `io_socket_tcp_v6` | TCP IPv6 |
| `io_socket_udp` | `io_socket<AF_INET, SOCK_DGRAM, IPPROTO_UDP>` |
| `io_socket_udp_v6` | UDP IPv6 |
| `io_socket_raw` | `io_socket<AF_INET, SOCK_RAW, IPPROTO_RAW>` |
| `io_connection` | `io_transport_entity<AF_INET, e_connected>` |
| `io_net_interface` | `io_transport_entity<AF_INET, e_indirect>` |
| `io_listener` | `io_listener_entity<AF_INET>` |

### io_mapping_entity (`net.h:786`)

| Метод | Сигнатура | Constraints |
|-------|----------|-------------|
| `bind(sockaddr*, socklen_t)` | `→ bind_query` | — |
| `bind(in_addr_t, uint16_t)` | `→ bind_query` | `is_inet_domain` |
| `bind(string_view, uint16_t)` | `→ bind_query` | `is_inet_domain` |
| `connect(sockaddr*, socklen_t)` | `→ connect_query_t` | — |
| `connect(in_addr_t, uint16_t)` | `→ connect_query_t` | `is_inet_domain` |
| `connect(string_view, uint16_t)` | `→ connect_query_t` | `is_inet_domain` |

### io_stream_mode_entity (`net.h:710`)

| Метод | Сигнатура |
|-------|----------|
| `listen(int backlog=0)` | `→ listen_query` |
| `connect(...)` | `→ connect_query_t` (3 overloads) |

### io_connection = io_transport_entity<AF_INET, e_connected> (`net.h:387`)

| Метод | Возврат | Описание |
|-------|---------|----------|
| `send(const void*, size_t, flags=0)` | `send_query` | `co_await` → `int` (bytes sent) |
| `send(string_view, flags=0)` | `send_query` | |
| `send(vector<T>&, flags=0)` | `send_query` | |
| `send(array<T,N>&, flags=0)` | `send_query` | |
| `send(span<T>&, flags=0)` | `send_query` | |
| `recv(void*, size_t, flags=0)` | `recv_query` | `co_await` → `int` (bytes recv) |
| `recv(vector<T>&, flags=0)` | `recv_query` | |
| `recv(string&, flags=0)` | `recv_query` | |
| `recv(array<T,N>&, flags=0)` | `recv_query` | |
| `recv(span<T>&, flags=0)` | `recv_query` | |
| `recv_vec<T>(flags=0)` | `promise<expected<vector<T>,int>>` | eager |
| `recv_str(flags=0)` | `promise<expected<string,int>>` | eager |

### io_net_interface = io_transport_entity<AF_INET, e_indirect> (`net.h:387`)

| Метод | Возврат | Constraints |
|-------|---------|-------------|
| `sendto(void*, size_t, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `sendto(string_view, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `sendto(vector<T>&, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `sendto(string&, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `sendto(array<T,N>&, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `sendto(span<T>&, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `recv(...)` — те же 6 overloads что и `io_connection::recv` | `recv_query` | no constraint |
| `connect(...)` — 3 overloads | `connect_query_t` | `e_indirect` |

### io_listener (`net.h:639`)

| Метод | Возврат |
|-------|---------|
| `accept()` | `accept_query` → `co_await` → `io_connection` |
| `accept(sockaddr*, socklen_t*)` | `accept_query` |
| `accept(sockaddr_in&)` | `accept_query` |
| `accept(sockaddr_in6&)` | `accept_query` |

### io_connection_link (`net.h:289`)

Высокоуровневая обёртка над `io_connection`:
- `write(data)` / `writeln(fmt, args...)` — fire-and-forget (не требует `co_await`)
- `read(buf, len)` — `async<int>`, требует `co_await`
- `read_vec<T>()` — `async<expected<vector<T>,int>>`
- `read_str()` — `async<expected<string,int>>`

### TCP паттерн (клиент)

```cpp
ace::task tcp_client(std::string host, uint16_t port) {
    auto mapping = co_await ace::net::io_socket_tcp();
    if (not mapping) co_return;  // operator bool: _fd > -1
    auto stream = co_await mapping.bind("0.0.0.0", 0);
    if (not stream) co_return;
    auto conn = co_await stream.connect(host, port);
    if (not conn) co_return;
    int sent = co_await conn.send("hello", 5);
    auto result = co_await conn.recv_str();
    if (result) { std::string body = result.value(); }
}
```

### TCP паттерн (сервер)

```cpp
ace::task tcp_server() {
    auto mapping = co_await ace::net::io_socket_tcp();
    auto stream  = co_await mapping.bind("0.0.0.0", 8080);
    auto listener = co_await stream.listen(128);
    while (true) {
        auto conn = co_await listener.accept();
        auto data = co_await conn.recv_str();
    }
}
```

### UDP паттерн

```cpp
ace::task udp_client() {
    auto mapping = co_await ace::net::io_socket_udp();
    auto udp = co_await mapping.bind("0.0.0.0", 0);
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

### Connected UDP паттерн

```cpp
ace::task connected_udp() {
    auto mapping = co_await ace::net::io_socket_udp();
    auto conn = co_await mapping.connect("127.0.0.1", 2123);  // connected UDP
    co_await conn.send("ping", 4);
    char buf[1500];
    int n = co_await conn.recv(buf, sizeof(buf));
}
```

---

## Файлы и консоль

### ace::console (`console.h`)

| Метод | Сигнатура |
|-------|----------|
| `input()` | `static async<expected<string,int>>` — асинхронный stdin |
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
| `ace::fs::file_link` | `io_link` для открытого файла — `write()` / `read()` / `read_str()` |

---

## Таймауты

### ace::futures::timeout / expire (`futures/timeout.h`)

```cpp
co_await ace::futures::timeout(500ms);     // относительный
co_await ace::futures::timeout(5s);

auto deadline = clock::current_time() + 2s;
co_await ace::futures::expire(deadline);   // абсолютный
```

**Внутреннее устройство:** `timeout_conductor` помещает задачу в `clock::subscribe()` (иерархическое колесо времени). Когда время истекает, `clock::ping()` возвращает задачу через `runner::reattach()`.

### Гонка recv с таймаутом

```cpp
char buf[4096];
auto result = co_await (
    conn.recv(buf, sizeof(buf)) or
    ace::futures::timeout(5s)
);
// result: std::variant<int,int>
if (auto* p = std::get_if<0>(&result)) {
    // recv выиграл, *p = bytes received
} else {
    // таймаут выиграл
}
```

---

## Комбинаторы

### `operator or` (гонка) — `core/compose.h:701`

| Операнды | Результат |
|---------|-----------|
| `void or void` | `int` (0 = left won, 1 = right won) |
| `T or void` | `std::optional<T>` |
| `void or T` | `std::optional<T>` |
| `T or U` | `std::variant<T,U>` |

### `operator and` (параллельное ожидание) — `core/compose.h:725`

| Операнды | Результат |
|---------|-----------|
| `void and void` | `void` |
| `T and void` | `T` |
| `void and T` | `T` |
| `T and U` | `std::tuple<T,U>` |

### `operator >>` (монадический пайп) — `core/compose.h:891`

```cpp
co_await (fetch_user() >> process_user);
// co_await fetch_user(), результат передаётся в process_user, co_await process_user
```

### `compose(sender, responder)` — `core/compose.h:388`

```cpp
co_await compose(read_data, process_data);
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

Конфигурация: `ace::core::s_dispatcher_config._runners_amount = 4; ace::reload();`

### spawn (`futures/spawn.h`)

```cpp
auto handle = co_await ace::spawn(worker());
// handle: async_handle
co_await handle.join();  // дождаться завершения
handle.cancel();         // отменить
handle.done();           // проверить завершение
```

**`co_await spawn()` НЕ суспендит вызывающего** — возвращает управление немедленно. Задача запускается на том же раннере. `_roaming = false` (привязана к раннеру).

### post (`futures/post.h`)

Как `spawn`, но задача помещается в НАЧАЛО очереди раннера (приоритет). `_roaming = false`.

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

### channel<T> (`futures/channel.h`)

MPMC (multi-producer/multi-consumer) канал на lock-free очереди.

| Метод | Сигнатура | Описание |
|-------|----------|----------|
| `push(T&)` / `push(T&&)` | `bool` | Синхронный push. `false` если буфер полон. |
| `operator<<(T)` | `channel&` | Сахар для `push()` |
| `pull()` | `pull_impl` | Awaitable. `co_await` возвращает `T`. |
| `pending_push(T)` | `promise<>` | Асинхронный push — ждёт пока появится место |

```cpp
ace::futures::channel<int> ch;
ch.push(42);
ch << 99;
int val = co_await ch.pull();
```

**Фоновый воркер:**
```cpp
ace::task bg_worker(channel<int>& ch) {
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
    auto lock_future = co_await guard->capture();
    // --- критическая секция ---
    co_await lock_future;
    guard->sync();  // разблокировка (также авто-вызов в ~proxy())
    co_return;
}
```

---

## Диспетчер и раннеры

### dispatcher (`core/dispatcher.h:86`)

Синглтон, управляет N раннерами. Главный поток выполняет `runner[0]` внутри `run()`. Рабочие потоки (1..N-1) — `std::jthread`, вызывают `worker_round()` в цикле.

- `worker_round()` — ~1ms работы, затем sleep 1ms если idle
- `round_robin(task&&)` — распределяет задачи по кругу
- `fetch_config()` — перечитывает `_runners_amount` при `reload()`

### runner (`core/runner.h:51`)

Per-thread исполнитель. Три очереди:
- `_pool` — lock-free MPSC для локальных задач
- `_interthread_pool` — lock-free MPSC для кросс-поточных вставок
- `_vortex_pool` — низкоприоритетные (polling) задачи

| Метод | Назначение |
|-------|-----------|
| `attach(async&&)` | Добавить задачу в очередь |
| `attach_front(async&&)` | Добавить в начало очереди |
| `yank()` | Обработать одну задачу |
| `yank_vortex()` | Обработать vortex-задачу |
| `run()` | Обработать до 128 задач за раз |
| `reattach(task&&, runner*)` | Вернуть задачу владеющему раннеру |
| `velocity()` | `double` — скользящее среднее загрузки |

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
| `_frame_size` | Размер фрейма (0 = destroyed) |
| `_status` | `promise_lifecycle` |
| `watch(void*)` / `unwatch(void*)` | Управление weak refcount |
| `disown(void*)` | Декремент strong refcount |

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
| `cancel()` | Отменить корутину |
| `done()` | `bool` — `_frame_size == 0`? |
| `finished()` | `bool` — `_status == e_finished`? |
| `is_idle()` | `bool` — корутина не выполняется? |
| `forward(void*)` | Пробудить ожидающего |

### async_handle (`core/async_handle.h:100`)

Handle для spawn-нутых задач. Наследует `control_block_handle` (через `join_handler`).

| Метод | Описание |
|-------|----------|
| `join()` | `join_handler&` — `co_await` для ожидания завершения |
| `done()` | `bool` — завершена? |
| `cancel()` | Отменить |

---

## I/O слой

### io_query<Q> (`core/io.h:290`)

CRTP база для io_uring операций. `await_suspend()` submit-ит SQE через `kernel_controller`.

| Метод | Описание |
|-------|----------|
| `setup_query(observer*)` | `virtual bool` — submit SQE |
| `await_ready()` | всегда `false` |
| `await_resume()` | возвращает `_res` (результат CQE) |
| `on_result(int)` | вызывается при получении CQE |

### read_query / write_query / close_query (`core/io.h:366,399,427`)

| Тип | Конструктор |
|-----|------------|
| `read_query` | `(int fd, void* buf, unsigned nbytes, uint64_t offset=0)` |
| `write_query` | `(int fd, const void* buf, unsigned nbytes, uint64_t offset=0)` |
| `close_query` | `(int fd)` |

### io_entity<E> (`core/io.h:493`)

CRTP база для владельцев файловых дескрипторов.

| Метод | Описание |
|-------|----------|
| `consume(entity_t&)` | `static` — извлечь FD и создать новый entity |
| `extract()` | `tuple{_fd, _is_closed}` — украсть FD |
| `close()` | `close_query` — асинхронное закрытие |
| `is_closed()` | `bool` |
| `operator bool()` | `_fd > -1` — валидный FD? |

Поля: `_fd` (int), `_is_closed` (bool), `_guard` (io_guard).

### io_guard (`core/io.h:451`)

RAII: асинхронно закрывает FD в деструкторе. Использует `io_hanged` для fire-and-forget close, либо `schedule(pending_close)` если нет io_uring.

### io_link (`core/io.h:622`)

Высокоуровневый I/O поверх `io_entity`.

| Метод | Сигнатура |
|-------|----------|
| `write(data)` | `void` — 6 перегрузок (string_view, void*, vector, array, span…) |
| `writeln(fmt, args...)` | `void` — format + newline |
| `read(buf, len)` | `async<int>` — 6 перегрузок |
| `read_vec<T>()` | `async<expected<vector<T>,int>>` |
| `read_str()` | `async<expected<string,int>>` |

### io_hanged (`core/io.h:242`)

Пул fire-and-forget I/O команд для использования вне корутин (например, в деструкторах `io_guard`).

---

## io_uring

### kernel_controller (`core/services/kernelic.h:86`)

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
| `open(obs, path, flags, mode)` | `io_uring_prep_open` |
| `close(obs, fd)` | `io_uring_prep_close` |
| `cancel(obs, flags)` | `io_uring_prep_cancel` |
| `cancel_fd(obs, fd, flags)` | `io_uring_prep_cancel_fd` |
| `nop(obs)` | `io_uring_prep_nop` |
| `ping()` | Дренирует SQEs, обрабатывает CQEs |

**kernel_observer** (`kernelic.h:51`): полиморфный обработчик CQE. Имеет `_runner_identity`, `_on_cancel`, `_multishot`. `on_result(int res)` вызывается при получении CQE.

---

## Clock

### clock / multi_dial (`core/services/clock.h`)

Иерархическое колесо времени с O(1) вставкой и освобождением.

| Компонент | Описание |
|-----------|----------|
| `clock` | Thread-local vortex. `ping()` освобождает истекшие таймеры. |
| `multi_dial` | Полное колесо: 5 уровней (1ms → 256ms → 65s → 4.6h → 49d) |
| `dial` | Один уровень колеса |
| `time_slot` | Один слот в уровне |
| `clock_record` | Запись таймера: `duration` + `task` |

| Метод clock | Назначение |
|------------|-----------|
| `current_time()` | Кешированный `steady_clock::now()` |
| `subscribe(task&&, duration)` | Подписать задачу на таймер |
| `detach(node*)` | Отменить таймер |
| `ping()` | Освободить истекшие таймеры |

---

## Conductor

### conductor_slot (`core/traits/conduction.h:139`)

In-place storage для одного conductor'а (размер `ACE_CONDUCTOR_MEM_SIZE` = cache_line - bus_size).

| Метод | Назначение |
|-------|-----------|
| `operator=(const conductor_t&)` | Placement new копия |
| `operator=(conductor_t&&)` | Placement new move |
| `operator<<(carry_t&)` | Украсть указатель из другого слота |
| `release()` | Уничтожить conductor |
| `get()` | `conductor_handle_t*` |

### runner_conductor_handle (`conduction.h:50`)

Абстрактный проводник для пересылки задач из раннера:
- `forward(async&&)` — переслать задачу
- `forward_node(node_t*)` — переслать узел
- `cancel()` — отменить

### control_conductor_handle (`conduction.h:98`)

Абстрактный проводник для control-block join/cancel:
- `forward(void* waiter)` — пробудить ожидающего
- `cancel()` — отменить

---

## Promise traits

### permanent / differed (`core/traits/promise.h:48,60`)

| Tag | `action()` | Поведение |
|-----|-----------|----------|
| `permanent` | `suspend_never` | Eager — корутина стартует сразу |
| `differed` | `suspend_always` | Lazy — ждёт `co_await` |

### promise_traits<D, T> (`core/traits/promise.h:184`)

Основной promise type. Наследует `promise_return_traits<D,T>`.

| Метод | Назначение |
|-------|-----------|
| `await_transform(futureT&)` | 4 перегрузки: conductor-based (clears bus_future) и busy-polling (sets bus_future) |
| `operator new(size_t)` | Аллокация control_block + promise |
| `operator delete(void*)` | disown control block |
| `setup_trace()` | Выделить trace ID |
| Поля: `_busy_future`, `_block`, `_trace_id` |

---

## Tools

### queue<T> (`core/tools/queue.h:131`)

Интрузивная двусвязная FIFO очередь на `slab_mempool`.

| Метод | Назначение |
|-------|-----------|
| `enqueue(T&&)` | Добавить в конец |
| `dequeue()` | Извлечь из начала (возвращает T) |
| `pop()` | Извлечь узел без разрушения |
| `empty()` | `bool` |
| `remove_node(node*)` | Удалить конкретный узел |

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

4. **`io_connection::recv_str()` возвращает `promise<expected<...>>`** — eager корутина, требующая `co_await`.

5. **Хранение сокетов в классах** — OK через `std::optional<io_connection>` или как член класса. `io_entity` не copyable, но movable.

6. **`co_await` rvalue async** — `operator co_await()` работает только с rvalue для move-only типов.

7. **`schedule()` требует `ace::task`** — для типизированных корутин используй `task_wrap()`.

---

## Файловая карта

| Файл | Что содержит |
|------|-------------|
| `ace.h` | Master include: async, dispatcher, compose, spawn, post, reattach, get_runner, roaming |
| `core/async.h` | `async<T>`, `promise<T>`, `task`, `task_wrap`, `suspend`, promise_type |
| `core/async_handle.h` | `async_handle` (join/cancel/done), `join_handler` |
| `core/compose.h` | `or_await`, `and_await`, `or/and_await_composed`, `operator or/and/>>`, `compose()` |
| `core/control.h` | `control_block`, `control_block_handle`, `promise_lifecycle` |
| `core/dispatcher.h` | `dispatcher`, `schedule`, `run`, `empty`, `reload`, `interrupt`, `terminate` |
| `core/io.h` | `io_query`, `io_entity`, `io_link`, `io_guard`, `io_hanged`, `io_caster`, `any`, read/write/close_query |
| `core/runner.h` | `runner` (per-thread), `attach`, `reattach`, `yank`, `run`, `velocity` |
| `core/signal.h` | `signal_handler`, `sig_pipe_t`, `termination_signal`, `interruption_signal` |
| `net.h` | Все TCP/UDP типы: `io_socket`, `io_mapping_entity`, `io_stream_mode_entity`, `io_listener_entity`, `io_transport_entity`, `io_connection_link`, `send_query`, `recv_query`, `bind_query`, `connect_query`, `sendto_query`, `accept_query` |
| `console.h` | `ace::console::input()`, `println()`, `print()` |
| `fs.h` | `ace::fs::file`, `file_link` |
| `futures/channel.h` | `channel<T>` (MPMC), `channel_st<T>` (single-thread), `push`, `pull`, `pending_push` |
| `futures/cutex.h` | `cutex` (cooperative mutex), `guard` |
| `futures/timeout.h` | `timeout(duration)`, `expire(deadline)` |
| `futures/spawn.h` | `spawn(task)` — параллельный запуск |
| `futures/post.h` | `post(task)` — приоритетный запуск |
| `futures/reattach.h` | `reattach(runner*)` — миграция корутины |
| `futures/roaming.h` | `roaming(bool)` — флаг миграции |
| `futures/get_runner.h` | `get_runner` — текущий раннер |
| `futures/polling.h` | `polling(bool)` — флаг низкого приоритета |
| `core/services/kernelic.h` | `kernel_controller` (io_uring vortex), `kernel_observer`, все `io_uring_prep_*` |
| `core/services/clock.h` | `clock` vortex, `multi_dial` (временное колесо), `clock::subscribe()`, `clock::ping()` |
| `core/traits/conduction.h` | `runner_conductor_handle`, `control_conductor_handle`, `conductor_slot` |
| `core/traits/future.h` | `future_traits`, `busy_future_traits`, concepts (`is_future`, `is_awaitable`), type traits |
| `core/traits/promise.h` | `permanent`, `differed`, `promise_traits`, `promise_return_traits` |
| `core/traits/vortex.h` | `vortex_traits` CRTP для фоновых сервисов |
| `core/tools/queue.h` | `queue<T>`, `q_node<T>`, `slab_mempool<T>` |
| `core/tools/id_alloc.h` | `id_allocator`, `async_id_allocator` |
| `core/tools/macro.h` | `ACE_CACHE_LINE_SIZE`, `ACE_CONDUCTOR_MEM_SIZE`, `ACE_AWAIT_NODISCARD` |
| `core/tools/moving_average.h` | `moving_average` (sliding window) |
| `core/tools/lifetime.h` | `lifetime` (RAII debug tracer) |

---

## Исправления

### UDP bind: возврат правильного fd
**Файл:** `include/ace/net.h` → `bind_query::await_resume()`  
При успешном `bind()` для не-STREAM сокетов (UDP) передаётся `_entity._fd` (реальный дескриптор сокета) вместо `_res` (код возврата bind, равный 0). Это гарантирует, что `io_net_interface` имеет корректный fd для `sendto`/`recv`.

### io_connection_link::input_action: блокирующий recv
**Файл:** `include/ace/net.h` → `io_connection_link::input_action()`  
Использует `::recv()` напрямую вместо недоступного `recv_query` (который определён позже в `io_transport_entity` и не имеет forward-объявления). Контроллеры не используют `io_connection_link`, работая напрямую с `io_connection::recv()`.
