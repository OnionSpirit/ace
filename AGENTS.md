# ACE Framework — Complete LLM Navigation Index

## Оглавление

1. [Быстрый старт](#быстрый-старт)
2. [Точка входа: entry, co_main, config](#точка-входа)
3. [Корутины: async / promise / task / automaton](#корутины)
4. [Сеть: TCP / UDP / Raw](#сеть)
5. [Файлы и консоль](#файлы-и-консоль)
6. [Таймауты и таймеры](#таймауты)
7. [Комбинаторы: or / and / >>](#комбинаторы)
8. [spawn / post / schedule / reattach / roaming](#управление-задачами)
9. [Каналы и мьютексы](#каналы-и-мьютексы)
10. [Диспетчер и раннеры](#диспетчер-и-раннеры)
11. [Сигналы](#сигналы)
12. [Control block и async_handle](#control-block)
13. [I/O слой: io_query, io_entity, io_link](#io-слой)
14. [io_uring (kernel_controller)](#io_uring)
15. [Clock: иерархическое колесо времени](#clock)
16. [Router: маршрутизация futures](#router)
17. [Promise traits и память](#promise-traits)
18. [Service: фоновые сервисы](#service)
19. [Tools: omniptr, queue, id_alloc, moving_average, iovec_alloc](#tools)
20. [Важные ограничения и паттерны](#ограничения)
21. [Файловая карта (полная)](#файловая-карта)
22. [Тесты](#тесты)

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

**Альтернативный вход через `co_main`** (при `-Dentry_mode=weak` в meson):
```cpp
#include <ace/ace.h>
#include <ace/futures/timeout.h>  // ace::timeout (короткий алиас, требует ace.h раньше)

ace::async<int> co_main(int argc, char** argv) {
    co_await ace::timeout(500ms);
    co_return 0;
}
```

**Архитектура:** C++20 корутины + Linux `io_uring`. Один раннер на поток. Диспетчер распределяет задачи round-robin. Все операции асинхронные через `co_await`.

---

## Точка входа

### `ace::cfg::init()` — `core/entry.h:70`

Вызывается однократно перед стартом диспетчера. Устанавливает все параметры конфигурации из compile-time значений или пользовательских специализаций `ace_param<Tag>`.

### `ace::cfg::config` — `core/config.h:103`

| Поле | Линия | По умолчанию |
|------|-------|-------------|
| `_runners_amount` | 105 | 1 |
| `_emergency_default` | 110 | true — срабатывать ли backup-коллбекам на необработанных исключениях |

Глобальный экземпляр: `ace::cfg::g_config` (line 112).

### `co_main()` — `core/entry.h:100`

```cpp
auto co_main() -> ace::entry;                    // без аргументов
auto co_main(int argc, char** argv) -> ace::entry; // с аргументами
```

Тип `ace::entry` = `async<entry_result>` (line 89), где `entry_result` содержит `int code` (line 82-87).

---

## Корутины

### async / promise / task / automaton

| Тип | Файл | Поведение |
|-----|------|-----------|
| `ace::async<T>` | `core/async.h:610` | Ленивая (`lazy_rule`). `initial_suspend() = suspend_always`. Запускается при `co_await` / `schedule`. |
| `ace::promise<T>` | `core/async.h:614` | Eager (`eager_rule`). `initial_suspend() = suspend_never`. Запускается сразу. |
| `ace::automaton<T>` | `core/async.h:618` | Eager + `co_yield`. Использует `automaton_rule`. Поддерживает `ping()` для потребления `co_yield` по одному. |
| `ace::task` | `core/async.h:621` | `async<void>` — ленивая void-корутина. |
| `ace::suspend` | `core/async.h:646` | `std::suspend_always` — точка приостановки. |
| `ace::task_wrap(async&&)` | `core/async.h:625` | Оборачивает `async<T>` в `task`. Нужен для `schedule()` типизированной корутины. |
| `ace::omni_node` | `core/async.h:637` | Тип-agnostic указатель на pool-ноду (`reg_queue::node_t \| mpsc_queue::node_t`). |
| `ace::omni_runner` | `core/async.h:640` | Тип-agnostic указатель на runner или его pool. |
| `ace::runner_router` | `core/async.h:643` | Абстрактный интерфейс для маршрутизации задач из раннера. |

**Ключевое различие promise vs async vs automaton:**
```cpp
// promise — eager, начинает выполняться сразу при вызове
ace::promise<int> eager() {
    co_return 42;  // если нет co_await — выполнится до возврата из функции
}

// async — lazy, ждёт co_await или schedule
ace::async<int> lazy() {
    co_return 42;  // не выполнится, пока кто-то не сделает co_await
}

// automaton — eager, поддерживает co_yield (множественный выход значений)
ace::automaton<int> gen() {
    co_yield 1;    // доступно через ping() → optional<int>(1)
    co_yield 2;    // доступно через ping() → optional<int>(2)
    co_return 3;   // доступно через ping()/join() → optional<int>(3)
}

// task — lazy void, для schedule
ace::task work() {
    co_await eager();  // OK: co_await promise
    co_await lazy();   // OK: co_await async
    co_return;
}
```

### promise_type (внутреннее устройство)

`async<T,Rule>::promise_type` (`core/async.h:392`):
- `initial_suspend()` — `Rule::action()` (`suspend_always` / `suspend_never`). Устанавливает `_runner = get_current_pool()`.
- `final_suspend()` — `suspend_always` (static)
- `get_return_object()` → `async{coroutine_handle}`. Вызывает `setup_control_block()`.
- `return_value(v)` / `return_void()` — сохраняет значение, `status = e_finished`
- `unhandled_exception()` — `status = e_failed`, вывод в stderr
- `operator new(size_t)` — аллоцирует `control_block` перед promise
- `~promise_type()` — default
- **Поля:** `_runner_router` (router_slot_t), `_runner` (omni_runner), `_waiters`, `_self_router`, `_roaming`, `_polling`, `_yield_waiter`

### async:: методы

| Метод | Линия | Описание |
|-------|-------|----------|
| `async(async&&)` | `async.h:116` | Move-only, копирование удалено |
| `is_exist()` / `operator bool()` | `:152,157` | Активна ли корутина |
| `~async()` | `:188` | `release_future()`, `release_router()`, disown block |
| `is_resumable()` | `:226` | Нет busy future или busy future готов |
| `observe()` | `:240` | `control_block_handle` для join/cancel |
| `release_waiters()` | `:251` | Будит всех ожидающих через `push_node()` |
| `track()` | `:269` | `expected<size_t, string_view>` — trace ID |
| `prefetch()` | `:275` | Предварительный запуск lazy-корутины без ожидания |
| `await_ready()` | `:515` | Если `e_canceled` или `e_executed_with_value` — true. Иначе проверяет `_runner_router` и `is_resumable()` |
| `await_suspend(outer)` | `:542` | Копирует `_runner` из outer, переносит router через `operator<<` |
| `await_resume()` | `:556` | Возвращает `_return_value` или void |
| `awake(res*)` | `:579` | Возобновляет корутину из раннера |

### async_router (внутренний)

`async_router` (`async.h:284`) — наследник `control_router_handle`. Устанавливается в control_block при `setup_control_block()`.

| Метод | Линия | Описание |
|-------|-------|----------|
| `cancel()` | `:295` | Вызывает `_runner_router->cancel()` и `release()`, затем `status(e_canceled)` |
| `redirect(void*)` | `:305` | Регистрирует waiter через `_waiters->push_node()` |
| `return_value(void*)` | `:314` | Читает `_return_value` из promise (для valued корутин) |
| `yield_value(void*)` | `:324` | Читает co_yield значение из promise |
| `has_yield()` | `:337` | Проверяет `e_executed_with_value` статус |
| `set_yield_waiter(void*)` | `:347` | Устанавливает ожидающего для co_yield |
| `cancel_yield()` | `:357` | Отменяет ожидание co_yield |
| `destroy()` | `:367` | Вызывает `handle.destroy()` — ручное уничтожение фрейма |

### Правила (rules) — `core/traits/promise.h`

| Tag | Линия | `action()` | Поведение |
|-----|-------|-----------|----------|
| `lazy_rule<T>` | 116 | `suspend_always` | Lazy — ждёт `co_await` |
| `lazy_rule<void>` | 148 | `suspend_always` | Lazy void |
| `eager_rule<T>` | 177 | `suspend_never` | Eager — стартует сразу |
| `eager_rule<void>` | 208 | `suspend_never` | Eager void |
| `automaton_rule<T>` | 67 | `suspend_never` | Eager + co_yield; без cancel/join контрольного блока |

Концепты: `is_rule<R>` (line 234), `is_spawnable_rule<R>` (line 249), `is_automaton_rule<R>` (line 259).

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
| `socket_raw_v6` | IPv6 Raw |
| `connection` | `transport_entity<AF_INET, e_connected>` |
| `net_interface` | `transport_entity<AF_INET, e_indirect>` |
| `listener` | `listener_entity<AF_INET>` |

### socket_entity (`net.h:802`)

| Метод | Сигнатура | Constraints |
|-------|----------|-------------|
| `bind(sockaddr*, socklen_t)` | `→ bind_query` | — |
| `bind(in_addr_t, uint16_t)` | `→ bind_query` | `is_inet_domain` |
| `bind(string_view, uint16_t)` | `→ bind_query` | `is_inet_domain` |
| `connect(sockaddr*, socklen_t)` | `→ connect_query_t` | — |
| `connect(in_addr_t, uint16_t)` | `→ connect_query_t` | `is_inet_domain` |
| `connect(string_view, uint16_t)` | `→ connect_query_t` | `is_inet_domain` |

### stream_mode_entity (`net.h:726`)

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
| `sendto(array<T,N>&, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `sendto(span<T>&, flags, sockaddr*, socklen_t)` | `sendto_query` | `e_indirect` |
| `recv(...)` — те же 6 overloads что и `connection::recv` | `recv_query` | no constraint |
| `connect(...)` — 3 overloads | `connect_query_t` | `e_indirect` |

### listener (`net.h:655`)

| Метод | Возврат |
|-------|---------|
| `accept()` | `accept_query` → `co_await` → `connection` |
| `accept(sockaddr*, socklen_t*)` | `accept_query` |
| `accept(in_addr_t, uint16_t)` | `accept_query` |
| `accept(string_view, uint16_t)` | `accept_query` |

### connection_link (`net.h:289`)

Высокоуровневая обёртка над `connection` (наследует `io::link`):
- `write(data)` / `writeln(fmt, args...)` — fire-and-forget (не требует `co_await`)
- `read(buf, len)` — `async<int>`, требует `co_await`
- `read_buf()` — `async<io::input_t>`

### Все query-типы (net + io + fs)

| Тип | Файл:линия | Конструктор | `await_resume()` |
|-----|-----------|------------|-----------------|
| `socket<D,T,P>` | net.h:926 | `(flags=0)` | `socket_entity<D,T>` |
| `bind_query` | net.h:815 | `(socket_entity&&, sockaddr*, socklen_t)` | `stream_mode_entity` / `transport_entity` |
| `listen_query` | net.h:736 | `(stream_mode_entity&&, backlog)` | `listener_entity<D>` |
| `accept_query` | net.h:665 | `(listener*, sockaddr*, socklen_t*, flags)` | `connection` |
| `connect_query<E,D>` | net.h:234 | `(entity&&, sockaddr*, socklen_t)` | `transport_entity<D, e_connected>` |
| `send_query` | net.h:265 | `(fd, buf, len, flags)` | `int` |
| `sendto_query` | net.h:408 | `(fd, buf, len, flags, addr, addrlen)` | `int` |
| `recv_query` | net.h:436 | `(fd, buf, len, flags)` | `int` |
| `sendmsg_query` | net.h:459 | `(fd, msghdr*, flags)` | `int` |
| `recvmsg_query` | net.h:480 | `(fd, msghdr*, flags)` | `int` |
| `io::read_query` | io.h:321 | `(fd, buf, nbytes, offset=0)` | `int` |
| `io::write_query` | io.h:354 | `(fd, buf, nbytes, offset=0)` | `int` |
| `io::close_query` | io.h:382 | `(fd)` | `int` |
| `fs::file::open_query` | fs.h:127 | `(file&&, path, flags, mode)` | `file_link` |

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

### ace::console (`console.h:34`)

Статический класс для асинхронного stdin/stdout.

| Метод | Линия | Сигнатура |
|-------|-------|----------|
| `input()` | 43 | `static async<io::input_t>` — асинхронный stdin |
| `println(fmt, args...)` | 48 | `static void` — format + newline |
| `println(string_view)` | 52 | `static void` |
| `println()` | 56 | `static void` — пустая строка |
| `println(io::buffer&&)` | 60 | `static void` |
| `print(fmt, args...)` | 65 | `static void` — format без newline |
| `print(string_view)` | 69 | `static void` |
| `print(io::buffer&&)` | 73 | `static void` |

### ace::fs (`fs.h`)

| Тип | Линия | Описание |
|-----|-------|----------|
| `ace::fs::file(path)` | 111 | `io_entity` для файлов |
| `file.open(flags, mode)` | 158 | `→ open_query → co_await → file_link` |
| `file.open_rdonly()` | 164 | Открыть только на чтение |
| `file.open_wronly()` | 167 | Открыть только на запись |
| `file.open_rewrite()` | 161 | Открыть на перезапись |
| `ace::fs::file_link` | 53 | `io::link` для открытого файла — `write()` / `read()` / `read_buf()` |

---

## Таймауты

### ace::timeout / ace::expire (`futures/timeout.h`)

```cpp
co_await ace::timeout(500ms);     // относительный
co_await ace::timeout(5s);

auto deadline = clock::current_time() + 2s;
co_await ace::expire(deadline);   // абсолютный
```

**Внутреннее устройство:** `timeout` (line 51) принимает `duration<I,T>` (требует `std::is_integral_v<I>`). `timeout_router` (line 122) помещает задачу в `clock::subscribe()`. Когда время истекает, `clock::ping()` возвращает задачу через `runner::reattach()`. При отмене `cancel()` вызывает `clock::detach()` и `runner::reattach()` ноды. `expire` (line 98) наследует `timeout` и вычисляет duration от дедлайна.

> Короткие алиасы `ace::timeout` / `ace::expire` определены в `futures/timeout.h`
> под guard `ACE_H` — доступны, если `ace/ace.h` подключён раньше
> `ace/futures/timeout.h`. Без `ace.h` доступны только `ace::futures::timeout` /
> `ace::futures::expire`. То же правило для `ace::channel`, `ace::cutex`,
> `ace::tunnel`, `ace::polling` и остальных futures-типов.

### Гонка recv с таймаутом

```cpp
auto result = co_await (
    conn.recv(buf, sizeof(buf)) or
    ace::timeout(5s)
);
// result: std::variant<int,int>
// index 0 = recv выиграл, index 1 = таймаут выиграл
```

---

## Комбинаторы

### `operator or` (гонка) — `core/compose.h:739`

4 перегрузки (lvalue/rvalue комбинации).

| Операнды | Результат |
|---------|-----------|
| `void or void` | `int` (0 = left won, 1 = right won) |
| `T or void` | `std::optional<T>` |
| `void or T` | `std::optional<T>` |
| `T or U` | `std::variant<T,U>` |

### `operator and` (параллельное ожидание) — `core/compose.h:763`

4 перегрузки.

| Операнды | Результат |
|---------|-----------|
| `void and void` | `void` |
| `T and void` | `T` |
| `void and T` | `T` |
| `T and U` | `std::tuple<T,U>` |

### `operator >>` (монадический пайп) — `core/compose.h:922`

6 перегрузок: 4 для function pointer/coroutine, 2 для generic callables (лямбды, функторы).

```cpp
// Функция-указатель или non-capturing лямбда
co_await (fetch_user() >> process_user);

// Захватывающая лямбда (generic callable)
co_await (ha.ping() >> [&](std::optional<int> val) { ... });

// С async корутиной
co_await (source >> async_coroutine);

// С функцией без входа (void sender)
co_await (task() >> on_complete);
```

**Важно:** для capturing lambdas параметр должен быть по значению или `const&` (не `T&`), т.к. `compose` форвардит результат как rvalue reference.

### `compose()` — `core/compose.h:381`

6 перегрузок — ядро имплементации `operator>>`.

| # | Линия | Sender | Responder |
|---|-------|--------|-----------|
| 1 | 381 | valued | async корутина с входом |
| 2 | 395 | void | async корутина без входа |
| 3 | 408 | valued | function pointer с входом |
| 4 | 421 | void | function pointer без входа |
| 5 | 433 | valued | generic callable с входом |
| 6 | 452 | void | generic callable без входа |

### Цепочки (variadic)

`or_await_composed<F...>` и `and_await_composed<F...>` позволяют цепочки из 3+ futures:
```cpp
co_await (a and b and c);    // and_await_composed → tuple<A,B,C>
co_await (a or b or c);      // or_await_composed → variant / optional / int
```

Внутренне: observers заводятся на `attach_front` текущего раннера.

---

## Управление задачами

### schedule / run / empty / reload / reset_signal (`core/dispatcher.h`)

| Функция | Линия | Назначение |
|---------|-------|-----------|
| `ace::schedule(task&&, runner* = nullptr)` | 245 | Поставить задачу в event loop. `runner*` — на конкретный раннер. |
| `ace::run()` | 289 | Запустить event loop. БЛОКИРУЕТ вызывающий поток. |
| `ace::empty()` | 211 | `bool` — все раннеры idle? |
| `ace::reload()` | 224 | Переконфигурировать количество раннеров. |
| `ace::interrupt()` | 334 | Послать сигнал `e_break` |
| `ace::terminate()` | 341 | Послать сигнал `e_shutdown` |
| `ace::reset_signal()` | 325 | Слить signal pipe |

Конфигурация: `ace::cfg::g_config._runners_amount = 4; ace::reload();`

### spawn (`futures/spawn.h:42`)

```cpp
// void-таска
auto handle = co_await ace::spawn(worker());
// valued-таска (valued join)
auto handle_int = co_await ace::spawn(async_int_task());
// handle: async_handle / async_handle<int>
co_await handle.join();  // дождаться завершения → bool / std::optional<T>
handle.cancel();         // отменить
handle.done();           // проверить завершение
```

**`co_await spawn()` НЕ суспендит вызывающего** — `await_suspend()` возвращает `false`. Задача `attach()` в конец очереди. `_roaming = false`.

**Valued spawn:** `spawn<resume_t>` оборачивает не-void корутины через `runner::carrier()` — цикл, который проходит через все suspension point'ы пока корутина не завершится. `join()` возвращает `std::optional<resume_t>` (nullopt если не `e_finished`).

### post (`futures/post.h:42`)

Как `spawn`, но задача `attach_front()` в НАЧАЛО очереди (приоритет). `await_suspend()` возвращает `true` (суспендит).

### roaming / reattach / get_runner

| Функция | Файл:линия | Назначение |
|---------|-----------|-----------|
| `co_await ace::roaming(bool)` | `futures/roaming.h:39` | Разрешить/запретить миграцию между раннерами. Не суспендит. |
| `co_await ace::reattach(runner*)` | `futures/reattach.h:40` | Мигрировать на конкретный раннер. Суспендит если раннер отличается. |
| `co_await ace::get_runner{}` | `futures/get_runner.h:33` | Получить `runner*` текущего раннера. Суспендит. |

### polling (`futures/polling.h:26`)

```cpp
co_await ace::polling(true);  // пометить задачу как низкоприоритетную (service)
```

Не суспендит (`await_suspend` возвращает `false`).

### backup / insure / emergency (`futures/backup.h`)

```cpp
co_await ace::backup([]{ cleanup(); });          // постоянный коллбек — fire при отмене (LIFO)
co_await ace::backup(cleanup_task());            // payload может быть ace::task — co_await-ится до конца
co_await ace::insure([]{ rollback(); });         // одноразовая страховка на СЛЕДУЮЩУЮ co_await/co_yield:
                                                 //  - отмена на этой операции → коллбек выполняется
                                                 //  - операция пройдена → страховка снимается
                                                 //  - новая регистрация backup/insure вытесняет её
co_await ace::emergency(false);                  // не срабатывать backup на исключениях (дефолт — true,
                                                 // конфигурируется через ace::cfg::g_config._emergency_default)
```

**Fire:** все коллбеки (callable и task) складываются в одну fire task, планируемую в раннер отменённой корутины (`runner::attach`; нет runner → `ace::schedule`). Fire task идёт в обратном порядке: callable — вызов, `ace::task` — `co_await` до завершения. Точки fire: `async_router::cancel()` и `~async()` (гейт: `e_finished` — пропуск; `e_failed && !_emergency` — пропуск). При OOM при создании fire task — `throw std::runtime_error("failed to init backup context. out of memory.")`.

---

## Каналы и мьютексы

### channel\<T\> — `ace::channel` / `futures/channel.h:83`

MPMC (multi-producer/multi-consumer) канал. Шаблонные параметры: `data_t`, `data_allocation_v` (по умолчанию `e_dynamic`), `access_mode_v` (`e_mpmc`), `data_buffer_size_v` (1).

| Метод | Линия | Описание |
|-------|-------|----------|
| `push(T&)` / `push(T&&)` | 150,157 | Синхронный push. `false` если буфер полон. |
| `operator<<(T)` | 188,194 | Сахар для `push()` |
| `pull()` | 182 | Awaitable (`pull_impl`). `co_await` возвращает `T`. |
| `operator>>(T&/T&&)` | 200,206 | `task` — альтернативный pull через `co_await` |
| `pending_push(T)` | 163 | `promise<>` — асинхронный push, ждёт пока появится место |
| `notify()` (private) | 134 | Пробуждает одного ожидающего |

**Алиасы в `ace::tunnel::dyn` (доступны при подключённом раньше `ace.h`):**
| Алиас | Тип |
|-------|-----|
| `dyn::local<T>` | `channel<T, e_dynamic, e_regular>` |
| `dyn::bridge<T>` | `channel<T, e_dynamic, e_spsc>` |
| `dyn::funnel<T>` | `channel<T, e_dynamic, e_mpsc>` |
| `dyn::bus<T>` | `channel<T, e_dynamic, e_mpmc>` |

```cpp
ace::tunnel::dyn::bus<int> ch;
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
co_await ace::spawn(bg_worker(ch));
```

### cutex — cooperative mutex (`futures/cutex.h`)

| Тип | Линия | Описание |
|-----|-------|----------|
| `ace::cutex` | 174 | Сам мьютекс |
| `ace::guard(mtx)` | 302 | Алиас `cutex::proxy` — RAII guard |

```cpp
ace::cutex mtx;

ace::task critical_section() {
    volatile auto guard = ace::guard(mtx);
    co_await guard.capture();
    // --- критическая секция ---
    guard.release();  // разблокировка (также авто-вызов в ~proxy())
    co_return;
}
```

**Методы `cutex::proxy` (line 207):**
| Метод | Линия | Описание |
|-------|-------|----------|
| `capture()` | 235 | Захват без миграции |
| `sync()` | 254 | Захват с разрешением миграции на другой раннер |
| `release()` | 273 | Разблокировка (возвращает `promise<>`) |

**Внутреннее устройство:** `cutex_control` (line 56) с атомарным `_users`. `cutex_router` (line 321) помещает ожидающие задачи в `roaming_mpsc_queue`. `cancel()` — no-op. При освобождении `notify()` пробуждает следующего.

---

## Диспетчер и раннеры

### dispatcher (`core/dispatcher.h:69`)

Синглтон, управляет N раннерами. Главный поток выполняет `runner[0]` внутри `run()`. Рабочие потоки (1..N-1) — `std::jthread`, вызывают `worker_round()` в цикле.

- `worker_round()` — ~1ms работы, затем sleep 1ms если idle
- `round_robin(task&&)` — распределяет задачи по кругу
- `fetch_config()` — перечитывает `_runners_amount` при `reload()`
- `_min_service_skips = 3` — минимальные пропуски сервисов

### runner (`core/runner.h:51`)

Per-thread исполнитель. Три очереди:
- `_pool` — lock-free reg_queue для локальных задач
- `_insert_pool` — lock-free mpsc_queue для кросс-поточных вставок
- `_service_pool` — низкоприоритетные (polling) задачи

| Метод | Линия | Назначение |
|-------|-------|-----------|
| `attach(async&&)` | 182 | Добавить задачу в очередь (void напрямую, valued через `carrier()`) |
| `attach_front(async&&)` | 192 | Добавить в начало очереди |
| `carrier(async<T>*)` | 208 | Цикл-обёртка для valued-тасок: проходит suspension point'ы через `carrier_suspend` |
| `carrier(automaton)` | 215 | Версия для automaton через `automaton_suspend` |
| `yank()` | 154 | Обработать одну задачу из `_pool` или `_insert_pool` |
| `yank_service()` | 160 | Обработать service-задачу |
| `run()` | 174 | Обработать до 128 задач за раз |
| `reattach(omni_node&, runner)` | 106 | Вернуть ноду владеющему раннеру (4 перегрузки: lvalue/rvalue) |
| `reattach_front(omni_node&, runner)` | 120 | Вернуть ноду в начало очереди (4 перегрузки) |
| `fetch_task_node()` | 204 | Извлечь ноду из `_pool` или `_insert_pool` |
| `velocity()` | 133 | `double` — скользящее среднее загрузки |
| `upgrade_velocity(duration)` | 145 | Увеличить velocity на основе времени ожидания |

---

## Сигналы

### signal_handler (`core/signal.h:38`)

| Тип | Линия | Описание |
|-----|-------|----------|
| `signal_handler` | 38 | Абстрактный: `virtual async<signal_trivial_orders> action() = 0` |
| `termination_signal` | 53 | `action()` → `e_shutdown` |
| `interruption_signal` | 62 | `action()` → `e_break` |
| `sig_pipe_t` | 48 | `mpsc_queue<unique_ptr<signal_handler>>` — очередь сигналов |
| `make_signal<signal_t>(args...)` | 74 | Фабрика (template) |

### signal_trivial_orders (`core/signal.h:28`)

```cpp
enum signal_trivial_orders { e_shutdown, e_idle, e_break };
```

---

## Control block

### control_block (`core/control.h:69`)

Intrusive reference-counted блок управления временем жизни корутины.

| Поле | Линия | Описание |
|------|-------|----------|
| `_refcount` | 72 | Счётчик наблюдателей (24 бита). Начальное: 1 (сама корутина). |
| `_status` | 73 | `promise_lifecycle` (8 бит) |
| `_frame_size` | 73 | Размер фрейма (32 бита; 0 = destroyed) |
| `_control_router` | 76 | `async_router_handle*` — router для cancel/redirect |

| Статический метод | Линия | Описание |
|-------------------|-------|----------|
| `is_untracked(void*)` | 87 | `_refcount == 0` |
| `track(void*)` | 96 | Инкремент refcount |
| `untrack(void*)` | 104 | Декремент refcount |
| `get_block_from_address(void*)` | 111 | Получить block из promise address |

Константа: `control_block_size` (line 116).

### promise_lifecycle (`core/control.h:48`)

```cpp
enum promise_lifecycle : uint8_t {
    e_failed,               // Корутина завершилась с исключением
    e_inited,               // Только создана; runner pool ещё не назначен
    e_executed,             // Приостановлена нормально (ожидает future)
    e_executed_with_value,  // co_yield — значение доступно, корутина приостановлена
    e_finished,             // Достигла co_return успешно
    e_canceled,             // Отменена
};
```

### control_block_handle (`core/control.h:140`)

Copyable внешний handle для наблюдения за корутиной.

| Метод | Линия | Описание |
|-------|-------|----------|
| `cancel()` | 183 | Отменить корутину (вызывает `_control_router->cancel()`, затем `release()`) |
| `is_idle()` | 191 | `bool` — `_block == nullptr` |
| `done()` | 197 | `bool` — `e_failed`, `e_canceled`, или `e_finished` |
| `finished()` | 208 | `bool` — `_status == e_finished` |
| `return_value(void*)` | 217 | Прочитать return value valued-корутины |
| `yield_value(void*)` | 228 | Прочитать co_yield значение |
| `has_yield()` | 238 | `bool` — `e_executed_with_value`? |
| `set_yield_waiter(void*)` | 243 | Установить ожидающего для co_yield |
| `cancel_yield()` | 248 | Отменить ожидание co_yield |
| `forward(void*)` | 258 | Передать waiter'а через `_control_router->redirect()` |
| `release()` (private) | 144 | При `untrack → true`: вызывает `destroy()` |

### async_handle (`core/async_handle.h:149`)

Handle для spawn-нутых задач.

| Метод | Линия | Описание |
|-------|-------|----------|
| `join()` | 183 | Для automaton: `automaton_join_handler` (ping + cancel). Иначе: `join_handler` (ждёт завершения). `co_await` → `bool` / `std::optional<resume_t>`. |
| `ping()` | 190 | Только для automaton. Возвращает `ping_handler<resume_t>`. `co_await` → `std::optional<resume_t>` — следующее co_yield значение. |
| `done()` | 194 | `bool` — завершена? |
| `cancel()` | 196 | Отменить |
| `~async_handle()` | 181 | Авто-cancel для automaton |

**Внутренние handler'ы:**
| Handler | Линия | Назначение |
|---------|-------|-----------|
| `join_handler<resume_t>` | 22 | Ожидание завершения обычных задач |
| `ping_handler<resume_t>` | 57 | Потребление одного co_yield из automaton |
| `automaton_join_handler<resume_t>` | 100 | ping + отмена после получения значения |

---

## I/O слой

### io::query\<Q\> (`io.h:244`)

CRTP база для io_uring операций. Наследует `future_traits` и `kernel_observer`. `await_suspend()` submit-ит SQE через `kernel_controller`.

| Метод | Линия | Описание |
|-------|-------|----------|
| `query_router` | 262 | Router: сохраняет `omni_node` в `_waiter`, пересылает через `redirect()` |
| `await_ready()` | 288 | всегда `false` |
| `await_suspend(auto)` | 290 | Submit SQE, установка query_router |
| `on_result(int)` | 305 | Сохраняет `_res`, делает `runner::reattach(_waiter)` |

**Поля:** `_waiter` (omni_node), `_res` (int, default INT_MIN), `_fd` (int), `_is_silent` (bool)

### io::buffer (`io.h:460`)

Scatter-gather буфер на цепочке `iovec`.

| Метод | Линия | Описание |
|-------|-------|----------|
| `expand(len)` | 752 | Выделить память для чтения |
| `append(data)` | 674 | Добавить данные (множество перегрузок) |
| `appendln(fmt, args...)` | 704 | format + newline |
| `prepend(data)` | 711 | Добавить в начало |
| `assemble()` | 792 | Построить `msghdr*` из цепочки iovec |
| `disassemble()` | 813 | Сбросить msghdr |
| `clone()` | 823 | Глубокая копия |
| `shape(size_t)` | 758 | Установить фиксированный размер |
| `clear()` | 847 | Освободить всю память |
| `len()` | 867 | Текущая длина |
| `as<T>()` | 741 | Конвертировать (`std::string`, `std::vector<std::byte>`) |

### io::entity\<E\> (`io.h:990`)

CRTP база для владельцев файловых дескрипторов.

| Метод | Линия | Описание |
|-------|-------|----------|
| `consume(entity_t&)` | 1002 | `static` — извлечь FD и создать новый entity |
| `extract()` | 1036 | `tuple{_fd, _is_closed}` — украсть FD |
| `close()` | 1047 | `close_query` — асинхронное закрытие |
| `is_closed()` | 1029 | `bool` |
| `operator bool()` | — | `_fd > -1` — валидный FD? |

Поля: `_fd` (int), `_is_closed` (bool), `_guard` (io::guard).

### io::guard (`io.h:949`)

RAII: асинхронно закрывает FD в деструкторе. Использует `io::hanged::command` для fire-and-forget close, либо `schedule(pending_close)` если нет io_uring.

### io::link (`io.h:1071`)

Высокоуровневый I/O поверх `io::entity`.

| Метод | Линия | Сигнатура |
|-------|-------|----------|
| `write(data)` | 1152 | `void` — 6 перегрузок (string_view, void*, vector, array, span…) |
| `writeln(fmt, args...)` | 1132 | `void` — format + newline |
| `read(buf, len)` | 1198 | `async<int>` — 6 перегрузок |
| `read_buf()` | 1224 | `async<io::input_t>` |

### io::any (`io.h:406`)

Минимальный type-erased holder для ассоциации данных с FD.

| Метод | Линия | Описание |
|-------|-------|----------|
| `any(data_t&&)` | 438 | Конструктор с type erasure |
| `release()` | 447 | Освободить данные |

### io::hanged (`io.h:903`)

Thread-local пул fire-and-forget I/O команд для деструкторов `io::guard`.

### Концепты

| Концепт | Линия | Описание |
|---------|-------|----------|
| `is_query<T>` | 49 | Имеет `setup_query(kernel_observer*) → bool` |
| `is_entity<T>` | 62 | Имеет `_fd` (int) и `_is_closed` (bool) |

---

## io_uring

### kernel_controller (`services/kernelic.h:87`)

Thread-local service. Каждый раннер имеет свой экземпляр с собственным `io_uring` ring (4096 entries).

| Метод | Линия | io_uring обёртка |
|-------|-------|-----------------|
| `socket(obs, domain, type, proto, flags)` | 169 | `io_uring_prep_socket` |
| `bind(obs, fd, addr, addrlen)` | 193 | `io_uring_prep_bind` |
| `connect(obs, fd, addr, addrlen)` | 195 | `io_uring_prep_connect` |
| `listen(obs, fd, backlog)` | 197 | `io_uring_prep_listen` |
| `accept(obs, fd, addr, addrlen, flags)` | 199 | `io_uring_prep_accept` |
| `send(obs, fd, buf, len, flags)` | 211 | `io_uring_prep_send` |
| `send_zc(obs, fd, buf, len, flags, zc_flags)` | 213 | `io_uring_prep_send_zc` |
| `send_zc_fixed(obs, fd, buf, len, flags, zc_flags, buf_index)` | 217 | `io_uring_prep_send_zc_fixed` |
| `sendto(obs, fd, buf, len, flags, addr, addrlen)` | 221 | `io_uring_prep_sendto` |
| `sendmsg(obs, fd, msg, flags)` | 223 | `io_uring_prep_sendmsg` |
| `recvmsg(obs, fd, msg, flags)` | 227 | `io_uring_prep_recvmsg` |
| `recv(obs, fd, buf, len, flags)` | 229 | `io_uring_prep_recv` |
| `read(obs, fd, buf, nbytes, offset)` | 231 | `io_uring_prep_read` |
| `write(obs, fd, buf, nbytes, offset)` | 233 | `io_uring_prep_write` |
| `writev(obs, fd, vec, len, offset, flags)` | 235 | `io_uring_prep_writev2` |
| `open(obs, path, flags, mode)` | 183 | `io_uring_prep_open` |
| `close(obs, fd)` | 191 | `io_uring_prep_close` |
| `cancel(obs, flags)` | 177 | `io_uring_prep_cancel` |
| `cancel_fd(obs, fd, flags)` | 179 | `io_uring_prep_cancel_fd` |
| `nop(obs)` | 167 | `io_uring_prep_nop` |
| `ping()` | 109 | Дренирует SQEs, обрабатывает CQEs |
| `submit()` | 127 | Универсальный шаблонный метод отправки IO |

**iovec аллокатор:** `iovec_allocate(n)`, `iovec_deallocate(iov)`, `iovec_pool_allocate(n)`, `iovec_pool_deallocate(iov, n)` (lines 265-281).

**kernel_observer** (`kernelic.h:52`): полиморфный обработчик CQE. Поля: `_runner_identity` (runner_pool_t*), `_on_cancel` (bool), `_multishot` (bool). `on_result(int res)` вызывается при получении CQE.

**kernel_entity** (`kernelic.h:293`): отложенное хранение SQE для overflow буферизации. До 8 параметров inline. Хранит type-erased указатель на io_uring функцию и action pointer.

---

## Clock

### clock / hierarchical_time_wheel (`services/clock.h`)

Иерархическое колесо времени с O(1) вставкой и амортизированным освобождением.

| Компонент | Линия | Описание |
|-----------|-------|----------|
| `clock` | 548 | Thread-local service. `ping()` продвигает колесо и истекает таймеры. |
| `hierarchical_time_wheel` | 288 | Полное колесо: до 7 уровней (1ms → 256ms → 65s → 4.6h → 49d → 34y → 2.3My); верхний уровень ограничен переполнением int64 (UB), лимит ~292 млн лет |
| `time_wheel` | 173 | Один уровень колеса (256 слотов) |
| `time_slot` | 114 | Один слот в уровне |
| `timer_record` | 81 | Запись таймера: `timepoint_t _expires` (абсолютный дедлайн) + `omni_node _context` |

**Типы:**
| Тип | Линия |
|-----|-------|
| `timepoint_t` | 46 — `time_point_cast<milliseconds>(steady_clock::now())` |
| `duration_t` | 52 — `duration_cast<milliseconds>` |
| `timer_node` | 109 — `q_node<timer_record>` |
| `cached_now()` | 58 — кэшированный `steady_clock::now()` (thread_local, обновление каждые 16 вызовов) |

| Метод clock | Линия | Назначение |
|------------|-------|-----------|
| `current_time()` | 554 | Кешированный `steady_clock::now()` |
| `subscribe(omni_node, duration)` | 558 | Подписать задачу на таймер |
| `detach(node*)` | 556 | Отменить таймер (возвращает ноду в runner) |
| `ping()` | 562 | Продвинуть колесо (`_wheel.advance()`) и вернуть `not empty()` |

**Методы hierarchical_time_wheel:**
| Метод | Линия | Назначение |
|-------|-------|-----------|
| `advance()` | 404 | Продвижение колеса на прошедшее время, истечение таймеров |
| `subscribe(omni_node, duration)` | 425 | Подписка в колесо (фаза нижних дисков — O(1), `lower_wheel_phase`) |
| `cascade(timer_node&&, progress)` | 461 | Перевставка каскадируемой записи по абсолютному дедлайну (re-select уровня) |
| `adjust()` | 498 | Синхронизация времени и сброс бюджета релиза |
| `detach(timer_node*)` | 514 | Отмена таймера с возвратом ноды в runner |

**Важно:** `timer_record::_context` — `omni_node` (не `task`). При отмене таймера `detach()` делает `runner::reattach()` ноды перед удалением из колеса. Пул записей: `slab_mempool<timer_record>` (thread-local). Каскад идёт **сверху вниз** (из грубого уровня в мелкий): `cascade_on_wrap()` → `cascade_slot()` → `cascade()`; бюджет релиза 1024 записи на ping ограничивает проход стрелки.

---

## Router

### router_slot (`core/traits/routing.h:156`)

In-place storage для одного router'а (размер `ACE_ROUTER_MEM_SIZE` = `hardware_constructive_interference_size - sizeof(size_t)`).

| Метод | Линия | Назначение |
|-------|-------|-----------|
| `operator=(const router_t&)` | 169 | Placement new копия |
| `operator=(router_t&&)` | 187 | Placement new move |
| `operator<<(carry_t&)` | 209 | Украсть router из другого слота через memcpy |
| `release()` | 225 | Уничтожить router (вызов виртуального деструктора) |
| `reset()` | 237 | Обнулить указатель без вызова деструктора |
| `get()` | 243 | `router_handle_t*` |
| `operator bool()` | 249 | Занят ли слот |

### runner_router_handle (`routing.h:49`)

Абстрактный интерфейс для маршрутизации задач из раннера:
- `redirect(omni_node node)` (virtual, line 62) — переслать ноду в хранилище future
- `cancel()` (virtual, line 71) — отменить (default: no-op)

### async_router_handle (control_router_handle) (`routing.h:87`)

Абстрактный интерфейс для control-block join/cancel:
- `redirect(void* waiter)` — pure virtual (96), пробудить ожидающего
- `cancel()` — pure virtual (101), отменить
- `return_value(void*)` — pure virtual (107), прочитать возвращаемое значение
- `yield_value(void*)` — virtual (115), прочитать co_yield, default: false
- `has_yield()` — virtual (121), проверка статуса, default: false
- `set_yield_waiter(void*)` — virtual (123), установить ожидающего co_yield, default: false
- `cancel_yield()` — virtual (125), отменить ожидание co_yield, default: false
- `destroy()` — pure virtual (130), ручное уничтожение фрейма

### Конкретные router'ы

| Router | Файл:линия | Наследует | `redirect()` | `cancel()` |
|--------|-----------|----------|-------------|------------|
| `query_router` | io.h:262 | `runner_router` | `_query->_waiter = node` | `kernel_controller::cancel()` |
| `timeout_router` | timeout.h:122 | `runner_router` | `clock::subscribe(node, dur)` | `clock::detach()` + `runner::reattach()` |
| `cutex_router` | cutex.h:321 | `runner_router` | `_cutex->_waiters.push_node(node)` | no-op |
| `channel_router` | channel.h:314 | `runner_router` | `_channel->push_node(node)` | reattach all + pop |
| `reattach_router` | reattach.h:82 | `runner_router` | Обновляет `_runner` + `reattach(node)` | — |
| `or_await_router` | compose.h:518 | `runner_router` | Сохраняет в `_waiter` | Отменяет оба observers |
| `and_await_router` | compose.h:567 | `runner_router` | Сохраняет в `_waiter` | Отменяет оба observers |
| `or_await_composed_router` | compose.h:660 | `runner_router` | Сохраняет в `_waiter` | Отменяет все observers |
| `and_await_composed_router` | compose.h:613 | `runner_router` | Сохраняет в `_waiter` | Отменяет все observers |
| `join_handler_router` | async_handle.h:203 | `runner_router` | `_handle.forward(node)` | — |
| `ping_router` | async_handle.h:219 | `runner_router` | `_handle.set_yield_waiter(node)` | `_handle.cancel_yield()` |
| `join_router` | async_handle.h:235 | `runner_router` | `_handle.forward(node)` | `_handle.cancel()` |
| `async_router` | async.h:284 | `control_router_handle` | `_waiters->push_node(waiter)` | `cancel` router + `e_canceled` |

---

## Promise traits

### promise_traits\<D, T\> (`core/traits/promise.h:294`)

Основной promise type. Наследует `promise_primitives` (line 39) и правило возврата.

| Метод | Линия | Назначение |
|-------|-------|-----------|
| `~promise_traits()` | 309 | Деструктор |
| `await_transform(std::suspend_always)` | 321 | Стандартная приостановка |
| `await_transform(std::suspend_never)` | 332 | Без приостановки |
| `await_transform(futureT&)` — lvalue future | 346 | Router-based suspension (clears `_busy_future`) |
| `await_transform(futureT&&)` — rvalue future | 360 | Router-based suspension |
| `await_transform(futureT&)` — lvalue busy_future | 376 | Active polling suspension (sets `_busy_future`) |
| `await_transform(futureT&&)` — rvalue busy_future | 390 | Active polling suspension |
| `operator new(size_t)` | 407 | Аллокация `control_block` + promise |
| `operator delete(void*, size_t)` | 422 | Освобождение |
| `setup_trace()` | 438 | Выделить trace ID |

**Поля:** `_busy_future`, `_block`, `_trace_id`, `_runner_router`, `_runner`, `_waiters`, `_self_router`, `_roaming`, `_polling`, `_yield_waiter`

---

## Service

### service_traits (`core/traits/service.h:94`)

CRTP база для фоновых polling-сервисов.

| Метод | Линия | Описание |
|-------|-------|----------|
| `service_traits()` | 147 | Конструктор — связывает detach-аксессоры по режиму |
| `service(sig_pipe_t&)` | 177 | Корутина вечного цикла `ping()` + ожидание сигналов |
| `respawn(runner*)` | 141 | Запустить service-корутину и сбросить detach-флаг |
| `touch(omni_runner)` | 220,224 | Активация service на раннере (респавн при detach) |
| `inspect()` (static) | 228 | Получить общий экземпляр без респавна |

**Режимы (enum `service_spawn_mode`, line 48):**
- `e_thread_shared` — один экземпляр на все потоки
- `e_thread_local` — отдельный экземпляр на каждый поток

**Концепты:**
| Концепт | Линия |
|---------|-------|
| `is_service_routine<V>` | 58 — имеет `ping() → bool` |
| `is_service_promise<V>` | 67 — имеет `ping() → promise<bool>` |
| `is_service_compatible<V>` | 76 |
| `is_service<V>` | 243 |

---

## Tools

### omniptr\<T, Ts...\> (`core/tools/omniptr.h:9`)

Тип-agnostic указатель для безопасного хранения одного из нескольких типов указателей.

| Метод | Линия | Назначение |
|-------|-------|-----------|
| `omniptr(void*)` | 15 | Неявное конструирование из void* |
| `omniptr(T*)` | 33 | Конструирование из конкретного типа |
| `as<T>()` | 58 | `static_cast<T*>(_ptr)` |
| `operator T*()` | 41 | Неявное приведение |
| `operator->()` | 69 | Доступ через первый шаблонный параметр |
| `reset()` | 77 | Обнулить |

Используется как `omni_node` (reg_queue::node_t | mpsc_queue::node_t) и `omni_runner` (runner | runner_pool_t).

### queue\<T\> (`core/tools/queue.h:130`)

Интрузивная двусвязная FIFO очередь на `slab_mempool`.

| Метод | Линия | Назначение |
|-------|-------|-----------|
| `enqueue(const T&)` | 163 | Добавить в конец (lvalue) |
| `enqueue(T&&)` | 175 | Добавить в конец (rvalue) |
| `enqueue(q_node<T>&&)` | 187 | Добавить готовую ноду |
| `dequeue()` | 199 | Извлечь из начала (возвращает T) |
| `pop()` | 207 | Извлечь узел без разрушения |
| `empty()` | 197 | `bool` |
| `remove_node(node*)` | 157 | Удалить конкретный узел |
| `unlink(node*)` | 146 | Отвязать узел без освобождения |

### slab_mempool\<T\> (`core/tools/queue.h:62`)

Slab-аллокатор. `alloc()` (line 98) / `free(node*)` (line 108). Чанки по 1024 узла (CHUNK_SIZE, line 67).

### q_node\<T\> (`core/tools/queue.h:35`)

Узел двусвязного списка. Методы: `data()` (43), `construct(T)` (46), `destruct()` (48), `remove()` (50).

### id_allocator / async_id_allocator (`core/tools/id_alloc.h`)

Lock-free аллокатор уникальных ID. `id_alloc()` (33) выделяет, `id_free()` (39) возвращает в пул. `async_id_allocator` (line 50) — thread-safe синглтон.

### moving_average (`core/tools/moving_average.h:27`)

Скользящее среднее с окном 4. `add(val)` — добавить значение, `value()` — текущее среднее, `clear()` — сброс.

### lifetime (`core/tools/lifetime.h:24`)

RAII debug tracer: логирует конструирование/разрушение. `track()` / `untrack()` — глобальное вкл/выкл.

### iovec_allocator (`core/tools/iovec_alloc.h:19`)

Thread-local аллокатор iovec буферов через `std::pmr`. `allocate(size_t)` (26), `deallocate(iovec*)` (42), `allocate_as<T>(n)` (50), `deallocate_as(void*, n)` (57). Внутренний `memory_controller` (63) с debug/release режимами.

### Макросы (`core/tools/macro.h`)

| Макрос | Линия | Значение |
|--------|-------|----------|
| `ACE_BUS_SIZE` | 20 | `sizeof(std::size_t)` |
| `ACE_ROUTER_MEM_SIZE` | 26 | `hardware_constructive_interference_size - ACE_BUS_SIZE` |
| `ACE_CACHE_LINE_SIZE` | 32 | `std::hardware_constructive_interference_size` |
| `ACE_CACHE_LINE(N)` | 40 | Zero-size padding sentinel |
| `ACE_AWAIT_NODISCARD` | 47 | `[[nodiscard("probably 'co_await' operator missing")]]` |
| `ACE_INCOMPATIBLE_COMPOSE_ERROR` | 49 | Сообщение об ошибке несовместимости типов в compose |
| `ACE_INLINE` | 51 | Force-inline (GCC/Clang) |
| `ACE_WEAK` | 59 | Weak symbol attribute |
| `ACE_IO_BUFFER_CHUNK_LIMIT` | 67 | 16 |
| `ACE_EMPTY_TYPE` | 43 | Zero-byte placeholder struct |

---

## Ограничения

1. **НЕ использовать `&&` и `||` с не-bool типами** — ACE переопределяет их для futures через `operator&&`/`operator||` из `compose.h`. Любое выражение вида `optional && bool` или `bool && function` будет поймано шаблонными перегрузками ACE. Решение: вложенные `if`.

2. **`ace::async<T>` — move-only**, копирование удалено.

3. **Entity state machine потребляет через move** — после каждого шага старая сущность недействительна.

4. **`connection::recv_buf()` возвращает `promise<io::input_t>`** — eager корутина, требующая `co_await`.

5. **Хранение сокетов в классах** — OK через `std::optional<connection>` или как член класса. `io::entity` не copyable, но movable.

6. **`co_await` rvalue async** — `operator co_await()` возвращает rvalue ref для move-only типов.

7. **`schedule()` требует `ace::task`** — для типизированных корутин используй `task_wrap()`.

8. **`operator>>` с capturing lambdas** — параметр лямбды должен быть по значению или `const&` (не `T&`), т.к. значение форвардится как rvalue.

9. **`automaton` coroutines** — `co_yield` значения потребляются через `ping()`. `join()` делает ping + cancel. Деструктор `async_handle<..., automaton_rule>` автоматически отменяет автоматон.

10. **НЕ использовать лямбда-корутины (coroutine lambdas)** — `[&]() -> ace::task {...}()` и т.п. **запрещены**. GCC размещает closure лямбды в кадре корутины так, что он накладывается на поле `_block` promise: вызов `observe()` (через `setup_control_block()`) затирает захваченные ссылки, и корутина читает мусор (ASan: heap-use-after-free / stack-use-after-scope). Баг пред-существующий (воспроизводится на чистом HEAD), проявляется при `observe()` перед `schedule()`/spawn, а также у task-payload в backup/insure. Решение: оформлять корутины как именованные функции/helper-методы с параметрами-ссылками. Обычные (не-корутинные) лямбды — можно.

11. **Короткие алиасы futures-типов** (`ace::timeout`, `ace::expire`, `ace::channel`, `ace::allocation_type`, `ace::access_mode`, `ace::tunnel::*`, `ace::polling`, `ace::cutex`, `ace::guard`, `ace::capture_future`, `ace::cutex_control`) определены в самих `futures/*.h` под `#ifdef ACE_H` — они доступны только если `ace/ace.h` включён **до** соответствующего futures-хидера. Иначе — только полные имена `ace::futures::X`.

    <!-- TODO (agent): реализовать поддержку лямбда-корутин — устранить коллизию
         closure лямбды с полем _block promise (GCC размещает closure в кадре на
         месте первого поля promise). Варианты: (а) явно хранить closure в promise
         (как параметр), (б) вычислять смещение closure и переносить _block,
         (в) вынести control_block в отдельную аллокацию. После фикса снять
         ограничение 10 и переписать тесты backup_fixture на лямбды. -->

---

## Файловая карта

| Файл | Что содержит |
|------|-------------|
| `ace.h` | Quick-start: entry, compose, spawn, post, reattach, get_runner, roaming, backup. Определяет guard `ACE_H` — короткие алиасы (`ace::timeout`, `ace::channel`, `ace::cutex`, `ace::tunnel`, ...) определяются в самих `futures/*.h` под `#ifdef ACE_H` и доступны только при включении `ace.h` раньше. |
| `core/entry.h` | `co_main()`, `ace::cfg::init()`, `ace::entry`, `ace::entry_result` |
| `core/config.h` | `ace::cfg::config`, `ace::cfg::g_config`, `ace::cfg::ace_param<Tag>`, `detail::resolve<Tag>()` |
| `core/async.h` | `async<T>`, `promise<T>`, `automaton<T>`, `task`, `task_wrap`, `suspend`, promise_type, async_router, omni_node/omni_runner/runner_router aliases |
| `core/async_handle.h` | `async_handle`, `join_handler`, `ping_handler`, `automaton_join_handler`, все router'ы для них |
| `core/compose.h` | `or_await`, `and_await`, `or/and_await_composed`, `compose()` (6 overloads), `operator or/and/>>` |
| `core/control.h` | `control_block`, `control_block_handle`, `promise_lifecycle`, `is_controled_promise` concept |
| `core/dispatcher.h` | `dispatcher`, `schedule`, `run`, `empty`, `reload`, `interrupt`, `terminate`, `reset_signal` |
| `core/runner.h` | `runner` (per-thread), `attach`, `carrier`, `carrier_suspend`, `automaton_suspend`, `reattach`, `yank`, `run`, `velocity` |
| `core/signal.h` | `signal_handler`, `sig_pipe_t`, `termination_signal`, `interruption_signal`, `make_signal<T>()` |
| `core/traits/future.h` | `future_handle`, `future_traits`, `busy_future_traits`, concepts (`is_future`, `is_awaitable`, `is_busy_future`), type traits (`resume_type`, `replace_type`, `unique_tuple_t`, `tuple_to_variant_t`, `at_pack`) |
| `core/traits/promise.h` | `lazy_rule`, `eager_rule`, `automaton_rule`, `promise_primitives`, `promise_traits`, concepts (`is_rule`, `is_spawnable_rule`, `is_automaton_rule`) |
| `core/traits/routing.h` | `runner_router_handle`, `async_router_handle` (control_router_handle), `router_slot` |
| `core/traits/service.h` | `service_traits` CRTP, `service_spawn_mode` enum, service concepts |
| `core/tools/omniptr.h` | `omniptr<T, Ts...>` — тип-agnostic указатель |
| `core/tools/queue.h` | `queue<T>`, `q_node<T>`, `slab_mempool<T>` |
| `core/tools/id_alloc.h` | `id_allocator`, `async_id_allocator` |
| `core/tools/iovec_alloc.h` | `iovec_allocator` — thread-local pmr-based iovec allocator |
| `core/tools/macro.h` | `ACE_CACHE_LINE_SIZE`, `ACE_ROUTER_MEM_SIZE`, `ACE_AWAIT_NODISCARD`, `ACE_INLINE`, `ACE_WEAK` |
| `core/tools/moving_average.h` | `moving_average` (sliding window 4) |
| `core/tools/lifetime.h` | `lifetime` (RAII debug tracer) |
| `net.h` | Все TCP/UDP типы: `socket`, `socket_entity`, `stream_mode_entity`, `listener_entity`, `transport_entity`, `connection_link`, все query-типы, `is_inet_domain`, `is_stream_type` |
| `io.h` | `io::query`, `io::entity`, `io::link`, `io::guard`, `io::hanged`, `io::buffer`, `io::any`, read/write/close_query, `is_query<E>`, `is_entity<E>` concepts |
| `console.h` | `ace::console::input()`, `println()`, `print()` |
| `fs.h` | `ace::fs::file`, `ace::fs::file_link`, `file::open_query` |
| `futures/channel.h` | `channel<T>` (MPMC), `pull_impl`, `channel_router`, aliases в `tunnel::dyn` и `tunnel::bounded` |
| `futures/cutex.h` | `cutex` (cooperative mutex), `cutex::proxy`, `capture_future`, `cutex_router`, `ace::guard()` алиас |
| `futures/timeout.h` | `timeout(duration)`, `expire(deadline)`, `timeout_router` |
| `futures/spawn.h` | `spawn(task)` — параллельный запуск (back of queue) |
| `futures/post.h` | `post(task)` — приоритетный запуск (front of queue) |
| `futures/reattach.h` | `reattach(runner*)` — миграция корутины |
| `futures/roaming.h` | `roaming(bool)` — флаг миграции |
| `futures/get_runner.h` | `get_runner` — текущий раннер |
| `futures/polling.h` | `polling(bool)` — флаг низкого приоритета |
| `futures/backup.h` | `backup(payload)` — постоянный коллбек при отмене (callable/task, LIFO); `insure(payload)` — одноразовая страховка на следующую co_await/co_yield; `emergency(bool)` — флаг срабатывания на исключениях; fire task + `promise_type::fire_backups()` |
| `services/kernelic.h` | `kernel_controller` (io_uring service), `kernel_observer`, `kernel_entity`, все `io_uring_prep_*`, iovec management |
| `services/clock.h` | `clock` service, `hierarchical_time_wheel`, `time_wheel`, `time_slot`, `timer_record`, `cached_now`, `clock::subscribe()`, `clock::ping()` |

---

## Тесты

Тесты находятся в `tests/`. Используют Google Test с fixture-based архитектурой. Сборка через meson (`-Dtests=true`), C++23, ASan. **Каждый тест регистрируется meson-ом отдельным процессом** (`discover_tests.py` + `--gtest_filter=@0@` — без пробелов внутри аргумента, иначе gtest не найдёт тест).

| Файл | Назначение |
|------|-----------|
| `tests/main.cpp` | GTest main |
| `tests/environment.h` | Все fixture-классы с хелпер-тасками (1098 строк) |
| `tests/tests.cpp` | `TEST_F` тесты (3746 строк, 237 тестов; отключённых нет — `cancel_spawned_with_channel` переоткрыт после фикса B7 в `BUGS_AND_BENCHMARKS.md`) |

### Fixture-классы (32 fixture)

| Fixture | Наследует | TearDown | Тесты |
|---------|----------|----------|-------|
| `base_fixture` | `::testing::Test` | — | Базовый: `once_suspend`, `channel_fetcher<T>`, `sleeper`, `fancy`, `fetch<T>(ch)`; +17 интеграционных тестов (kernelic, io_query, channel-варианты, reattach, udp, tcp) |
| `context_fixture` | `base_fixture` | — | 13: coroutine lifecycle, async move, track, observe, prefetch, task_wrap, automaton_no_cancel |
| `channel_fixture` | `base_fixture` | — | 1: channel send/receive |
| `timer_fixture` | `base_fixture` | reset runners | 10: or, and, timer, expire, timeout_zero/short/multiple, parallel |
| `yield_fixture` | `base_fixture` | reset runners | 7: automaton spawn/ping/join/post/cancel |
| `cutex_fixture` | `base_fixture` | reset runners + signal | 4: cutex race, rescheduling, cancel |
| `spawn_fixture` | `base_fixture` | — | 10: spawn, post, valued_spawn/post, cancel, join, composed_output |
| `socket_echo_fixture` | `base_fixture` | reset_signal | 2: TCP echo, zero-copy echo |
| `fs_fixture` | `base_fixture` | — | 4: filesystem write/read, open fail |
| `queue_fixture` | `::testing::Test` | — | 10: slab_mempool + queue operations |
| `omniptr_fixture` | `::testing::Test` | — | 10: omniptr all operations (+2 lifetime) |
| `id_alloc_fixture` | `::testing::Test` | — | 3: id alloc/free |
| `moving_average_fixture` | `::testing::Test` | — | 7: moving average edge cases |
| `future_traits_fixture` | `::testing::Test` | — | 8: compile-time concept/trait checks |
| `promise_traits_fixture` | `base_fixture` | — | 8: rule tags, return traits, operator new layout |
| `router_slot_fixture` | `::testing::Test` | reset_counter | 8 + 1: router slot copy/move/steal |
| `signal_fixture` | `base_fixture` | — | 4: signal push/pop |
| `control_block_fixture` | `::testing::Test` | — | 15: control_block lifecycle + handle ops |
| `runner_fixture` | `base_fixture` | — | 8: attach, run, velocity, move, suspending_task_run (с pump времени) |
| `dispatcher_fixture` | `base_fixture` | reset runners + signal | 7: schedule, run, reload, signals |
| `io_buffer_fixture` | `::testing::Test` | — | 24: buffer expand/append/prepend/assemble/clone |
| `io_entity_fixture` | `::testing::Test` | — | 9: entity lifecycle, move, extract, close, guard |
| `io_any_fixture` | `::testing::Test` | — | 6: type-erased any construction/move/destructor |
| `io_hanged_fixture` | `::testing::Test` | — | 5: fire-and-forget command pool |
| `console_fixture` | `::testing::Test` | — | 4: console print/println |
| `cross_mechanic_fixture` | `base_fixture` | reset runners + signal | 17: cross-subsystem integration (spawn + timeout + channel + cutex + or_ping_automaton + cancel_channel) |
| `spawn_extra_fixture` | `base_fixture` | — | 8: spawn join, cancel, done, post priority, roaming, polling |
| `compose_extra_fixture` | `base_fixture` | — | 3: or_await, and_await, operator>> pipe tests |
| `channel_extra_fixture` | `base_fixture` | — | 4: push/pull, shift operator, mpsc |
| `cutex_extra_fixture` | `base_fixture` | reset runners + signal | 5: proxy double capture/sync/destructor, try_lock |
| `get_runner_fixture` | `base_fixture` | — | 1: get_runner inside runner |

### Бенчмарки

`benchmarks/` — Google Benchmark, сборка `-Dbenchmarks=true` (цель `ace_benchmarks`).
21 бенчмарк (BM1-BM20): cutex race (capture/sync), таймеры (parallel/ordering/expire),
spawn (cancel/join/fire-forget), каналы (push_pull/pending_push), reattach-миграция,
automaton ping, compose (and/or/variadic), io_buffer (append/clone), pipe io_uring
roundtrip, schedule throughput. См. инвентарь в `BUGS_AND_BENCHMARKS.md`.

### Coverage

Цель 95%. Текущее значение: **94.3%** (gcov, meson per-test режим; см. `TEST_PLAN.md`).
Измерение требует симлинков `tests`/`include` внутри build-каталога (см. TEST_PLAN.md).

### Добавление новых тестов

1. Добавить хелпер-таски как методы fixture-класса в `tests/environment.h`
2. Написать `TEST_F(FixtureName, test_name) { ... }` в `tests/tests.cpp`
3. Общие `fetch<T>(ch)`, `channel_fetcher`, `sleeper` — уже в `base_fixture`
4. Переконфигурировать build-каталог (`meson setup build --reconfigure`), чтобы
   `discover_tests.py` зарегистрировал новый тест в meson.
