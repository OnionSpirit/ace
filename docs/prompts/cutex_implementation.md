# Промпт: Реализация компонента cutex

## 1. Обзор и назначение

**Cutex** — это **C**ooperative **U**serspace mu**TEX** (кооперативный пользовательский мьютекс) без системных вызовов (без syscall). Это future-объект, работающий целиком в приложении, предназначенный для синхронизации доступа к критическим секциям в асинхронной среде C++20 корутин.

### Ключевые отличия от системного мьютекса:
- **Без системных вызовов**: не использует `mutex`, `futex` или другие примитивы ОС
- **Полностью асинхронный**: интегрируется с фреймворком диспетчеризации ACE
- **Избегает busy-polling**: использует очередь ожидающих задач и conductor pattern для эффективной диспетчеризации
- **Future-based API**: работает через `co_await` для асинхронного захвата
- **Многопоточная поддержка**: при использовании с несколькими runner'ами

---

## 2. Архитектурные компоненты

### 2.1 `cutex_future` — Основной future объект

Это base класс, наследующий `future_traits<cutex_future>`, который реализует логику синхронизации мьютекса.

#### Состояние (члены класса):

```cpp
std::atomic<int> _users { 0 };                    // Счетчик активных владельцев мьютекса
nukes::dynamic::roaming_mpsc_queue<task> _waiters {};  // Очередь корутин, ожидающих захвата
std::atomic<runner_pool_t*> _runner_pool { nullptr };    // Пул runner'а для переноса захвата
bool _rescheduling { false };                     // Флаг режима переноса (rescheduling mode)
```

**Объяснение переменных:**
- `_users`: счетчик текущих владельцев мьютекса. Значение 0 означает свободен, значение > 0 означает заблокирован
- `_waiters`: динамическая MPSC-очередь (Multi-Producer, Single-Consumer) для хранения корутин, ожидающих освобождения мьютекса
- `_runner_pool`: указатель на пул runner'а, к которому был привязан первый захватывающий корутин. Используется для оптимизации переноса задач между runner'ами
- `_rescheduling`: флаг режима переноса. Когда включен, позволяет более оптимально переносить готовые корутины между runner'ами вместо привязки к одному конкретному

#### Критические методы:

**`bool try_lock() noexcept`**
- Попытка захватить мьютекс без блокировки
- Использует атомарный `fetch_add(1)` с memory order `acq_rel` (acquire-release)
- Возвращает `true` если захват успешен (счетчик был 0, т.е. корутин был первым)
- Возвращает `false` если мьютекс уже занят

**`bool await_ready() override`**
- Часть C++20 awaitable интерфейса
- Проверяет, готов ли future (не нужна ли приостановка)
- Возвращает `true` если `try_lock()` успешен (захват произошел без ожидания)
- Возвращает `false` если нужна приостановка (корутин будет добавлен в очередь)

**`bool await_suspend(auto coroutine)`**
- Вызывается при приостановке корутина
- **Критично**: устанавливает conductor для корутина, чтобы он мог быть перемещен из runner'а в очередь cutex'а вместо того, чтобы остаться в runner'е
- Сохраняет `_runner_pool` из обещания первого пришедшего корутина (для оптимизации переноса)
- Возвращает `true` (всегда приостанавливает, так как conductor будет обрабатывать дальнейшие события)

**`bool notify() noexcept`**
- Пробует пробудить одного ожидающего корутина из очереди
- Извлекает корутин из `_waiters` через `pop(waiter)`
- Если rescheduling режим **включен**:
  - Если корутин **не поддерживает roaming** (не может перемещаться между runner'ами): сохраняет его runner_pool
  - Если корутин **поддерживает roaming**: назначает ему текущий `_runner_pool`
- Если rescheduling **отключен**: оставляет runner_pool как есть
- Вызывает `waiter.release_future()` для отключения conductor
- Возвращает корутин в runner через `core::runner::reattach()`
- Возвращает `true` если корутин был успешно пробужден, `false` если очередь пуста

**`task pending_notify() noexcept`**
- Отложенная нотификация для разрешения deadlock'а
- Это корутина (возвращает `task`), которая:
  1. Пробует `notify()`
  2. Если не удалось (возвращен false) — приостанавливается через `co_await suspend()`
  3. Повторяет попытку в цикле до тех пор, пока счетчик `_users > 0`
  4. Выходит, когда все владельцы освободили мьютекс

---

### 2.2 `cutex` — Публичный интерфейс

Наследует `cutex_future` и защищает доступ через proxy pattern.

#### Методы:

**`cutex_future& capture() noexcept`**
- Приватный метод, возвращает reference на себя как на `cutex_future`
- Позволяет использовать через `co_await`
- Пример:
  ```cpp
  auto& fut = cutex_obj.capture();
  co_await fut;  // Приостанавливает до освобождения
  ```

**`void sync() noexcept`**
- **Критичный метод**: освобождает мьютекс
- Декрементирует счетчик `_users` через `fetch_sub(1)` с memory order `acq_rel`
- **Ключевая логика**:
  ```
  if (декремент вернул значение > 1 AND notify() вернул false):
      schedule(pending_notify())  // Планируем отложенную нотификацию
  ```
- Это разрешает deadlock сценарий, когда есть ожидающие корутины, но невозможно их пробудить немедленно

**Методы для управления режимом**:
- `void set_rescheduling(bool rs) noexcept` — включить/отключить режим переноса
- `bool get_rescheduling() const noexcept` — получить текущее состояние

---

### 2.3 `cutex::proxy` — Паттерн безопасной работы

Обеспечивает безопасную семантику capture/sync через RAII.

#### Члены:
```cpp
cutex& _cutex;
bool _is_synced { true };
```

#### Использование:
```cpp
ace::cutex my_cutex;
{
    volatile auto proxy = my_cutex;  // RAII proxy
    co_await proxy.capture();         // Захватить мьютекс
    // Критическая секция
    proxy.sync();                     // Освободить
} // Деструктор гарантирует sync(), если забыли вызвать вручную
```

#### Принцип:
- При создании `_is_synced = true`
- При `capture()`: проверяет, что был вызван `sync()` (иначе exception), затем устанавливает `_is_synced = false`
- При `sync()`: если `_is_synced == false`, то вызывает `_cutex.sync()` и устанавливает `_is_synced = true`
- При деструкции: вызывает `sync()` для гарантии освобождения

---

### 2.4 `cutex_conductor` — Диспетчер без busy-polling

Реализует `conductor_handler_t` интерфейс и интегрируется с runner'ом.

#### Определение:
```cpp
struct cutex_conductor : conductor_handler_t {
    cutex_future* _cutex;
    
    void forward(task&& ctx) override {
        // Непрерывно пытается добавить в очередь
        while (not _cutex->_waiters.push(std::move(ctx)));
    }
    
    void cancel() override { /* TODO */ }
    ~cutex_conductor() override = default;
};
```

#### Механизм работы:

1. **Установка conductor'а**: когда корутин приостанавливается в `await_suspend()`, в его обещание (`promise`) устанавливается `cutex_conductor{this}`

2. **Диспетчеризация runner'ом**: runner регулярно проверяет (`yank()`):
   - Если в обещании корутина установлен conductor
   - Если да — вместо добавления обратно в runner пул, передает корутин conductor'у через `forward()`

3. **Результат**: корутин попадает в `_waiters` очередь cutex'а, а не в runner пул (нет busy-polling, нет напрасных попыток резюме)

4. **Освобождение**: когда `sync()` вызывает `notify()`, пробужденный корутин возвращается в runner через `reattach()`, и затем может быть резюмирован

---

## 3. Детальный поток выполнения

### 3.1 Захват мьютекса (capture)

```
Корутин A вызывает: co_await cutex.capture()
    ↓
await_ready() вызывает try_lock()
    ↓
try_lock() проверяет _users.load(acq_rel)
    ├─ Если было 0 (свободен): fetch_add(1) возвращает 0 → await_ready() = true → корутин не приостанавливается
    └─ Если было > 0 (занят): fetch_add(1) возвращает > 0 → await_ready() = false → продолжить
    ↓
await_suspend(coroutine) вызывается
    ├─ Сохраняет runner_pool в _runner_pool (если еще не сохранен)
    ├─ Создает и устанавливает cutex_conductor в promise._future_conductor
    └─ Возвращает true (корутин приостанавливается)
    ↓
Runner обнаруживает conductor в promise
    ├─ Вместо добавления обратно в runner пул
    └─ Вызывает conductor.forward() → корутин добавляется в _waiters
    ↓
Корутин находится в _waiters, ждет пробуждения
```

### 3.2 Освобождение мьютекса (sync)

```
Корутин A вызывает: cutex.sync()
    ↓
_users.fetch_sub(1, acq_rel) возвращает старое значение
    ├─ Если вернул 1 (был последний владелец): 
    │   └─ Просто выход, нечего пробуждать
    │
    └─ Если вернул > 1 (были еще владельцы или ожидающие):
        ├─ Вызывает notify()
        │   ├─ Пробует pop() из _waiters
        │   ├─ Если успешно: пробуждает корутин и возвращает true
        │   └─ Если пусто: возвращает false
        │
        └─ Если notify() вернул false (очередь пуста или в процессе):
            └─ schedule(pending_notify())
                ├─ Создает корутину, которая в цикле пробует notify()
                ├─ Приостанавливается между попытками
                └─ Продолжает до полного освобождения

Пробужденный корутин B возвращается в runner через reattach()
    ↓
await_resume() вызывается (пусто)
    ↓
Корутин B входит в критическую секцию с гарантированным захватом
```

---

## 4. Edge Cases и Deadlock Scenarios

### 4.1 Deadlock Scenario — "Interrupted Queue Insertion"

**Проблема**:
- Поток A владеет cutex
- Поток B пробует захватить, но неудачен в `try_lock()`
- Поток B вызывает `await_suspend()` и создает conductor
- **ОС прерывает поток B перед тем, как он попадет в `_waiters.push()`**
- Поток A вызывает `sync()` → `notify()` → проверяет `_waiters.pop()` → **очередь пуста!**
- **Результат**: Поток B навеки заблокирован, ждет в `_waiters`, но никто его не пробуждает

**Решение**:
- `sync()` не только вызывает `notify()`, но и проверяет результат
- Если `notify()` вернул `false` (не смог пробудить), `sync()` планирует `pending_notify()`
- `pending_notify()` — это корутина, которая в цикле пробует `notify()`, пока счетчик `_users > 0`
- Это гарантирует, что даже если push в `_waiters` произошел с задержкой, он будет обнаружен

### 4.2 Scenario — "Multiple Waiters Race"

**Проблема**:
- Несколько потоков одновременно освобождают мьютекс
- Каждый может вызвать `notify()`, но только один корутин должен быть пробужден
- Если `_waiters.pop()` не atomic, возможны дубли или потери

**Решение**:
- `_waiters` — это MPSC очередь (MPSC гарантирует корректность pop'а)
- Только один корутин может быть pop'нут на одном вызове `notify()`
- Если есть еще ожидающие, `sync()` гарантирует `pending_notify()` для цепочки пробуждений

### 4.3 Scenario — "Rescheduling Optimization"

**Проблема**:
- Если мьютекс захватывается потоками из разных runner'ов, корутины могут мигрировать между пулами
- Без оптимизации можно потерять локальность кэша и производительность

**Решение — Rescheduling режим**:
- `_runner_pool` сохраняет пул первого захватившего корутина
- Когда `_rescheduling = true`:
  - Если пробуждаемый корутин **не поддерживает roaming** (`_roaming = false`): его runner_pool сохраняется и восстанавливается
  - Если **поддерживает roaming** (`_roaming = true`): он переназначается текущему `_runner_pool` для локальности

### 4.4 Scenario — "Nested Co_await Suspension"

**Проблема**:
- Корутин приостановлен в `await_suspend()`, но promise содержит conductor
- Если есть nested future, conductor должен пройти вверх по цепочке

**Решение**:
- В C++20 `await_suspend()` может передать conductor вверх через `outer.promise()._future_conductor << ...`
- ACE фреймворк поддерживает `conductor_carry` для передачи conductors между уровнями

---

## 5. Интеграция с фреймворком

### 5.1 Memory Ordering

Используй `std::memory_order_acq_rel` для всех операций над `_users`:
- `fetch_add(1, acq_rel)` при захвате — гарантирует acquire семантику (видимость предыдущих операций)
- `fetch_sub(1, acq_rel)` при освобождении — гарантирует release семантику (видимость для следующих операций)

Для `_runner_pool` используй:
- `store(..., memory_order_release)` при установке
- `load(memory_order_acquire)` при чтении

### 5.2 Взаимодействие с Runner

Runner регулярно вызывает `yank()` на каждом корутине:
1. `awake()` пытается резюме корутин
2. Если в `promise._future_conductor` установлен conductor:
   - Вместо добавления обратно в runner пул
   - Вызывает `conductor.forward(coroutine)`
3. Это критично для избежания busy-polling

### 5.3 Взаимодействие с Dispatcher

Используй глобальную функцию `ace::schedule()` для планирования `pending_notify()`:
```cpp
schedule(pending_notify());  // Планирует корутину в глобальный dispatcher
```

---

## 6. Требования к реализации

### Функциональные требования:

1. **Захват и освобождение**:
   - ✅ Корректно работает `try_lock()` с атомарными операциями
   - ✅ `await_ready()` возвращает правильный результат
   - ✅ `await_suspend()` правильно настраивает conductor
   - ✅ `sync()` правильно освобождает и пробуждает

2. **Очередь ожидающих**:
   - ✅ Корутины корректно добавляются в `_waiters` через conductor
   - ✅ `notify()` пробуждает корутины в порядке FIFO
   - ✅ Нет потерь или дупликатов при многопоточности

3. **Deadlock Prevention**:
   - ✅ `pending_notify()` корректно разрешает "interrupted queue insertion" scenario
   - ✅ Логика в `sync()` правильно решает, когда планировать `pending_notify()`

4. **Rescheduling режим**:
   - ✅ Сохранение и восстановление `_runner_pool`
   - ✅ Правильная обработка `_roaming` флага корутина
   - ✅ Корректное установление режима через setter/getter

5. **Proxy pattern**:
   - ✅ `proxy` гарантирует RAII семантику
   - ✅ Исключение при неправильном порядке вызовов
   - ✅ Деструктор гарантирует `sync()`

### Нефункциональные требования:

1. **Производительность**:
   - ✅ Нет syscall'ов
   - ✅ Нет busy-polling (conductor диспетчеризирует)
   - ✅ Минимальные atomic операции
   - ✅ Нет malloc/free при стандартном использовании (используй nukes::roaming_mpsc_queue)

2. **Безопасность потоков**:
   - ✅ Все операции над `_users` — atomically
   - ✅ Все операции над `_runner_pool` — atomically
   - ✅ MPSC очередь `_waiters` правильно обрабатывает multiproducer

3. **Интеграция**:
   - ✅ Совместимо с future_traits интерфейсом
   - ✅ Совместимо с conductor_handler_t интерфейсом
   - ✅ Использует task, runner_pool_t правильно

---

## 7. Требования к тестированию

Реализуй или убедись, что проходят следующие тесты:

### Unit тесты:

1. **`test_cutex_basic_capture_sync`**:
   - Захват без конкуренции
   - Синхронное освобождение
   - Проверка счетчика `_users`

2. **`test_cutex_try_lock`**:
   - `try_lock()` возвращает true при свободном мьютексе
   - `try_lock()` возвращает false при занятом

3. **`test_cutex_single_waiter`**:
   - Один корутин ждет, один освобождает
   - Корутин пробуждается корректно

### Race-condition тесты (уже существуют в tests.cpp):

1. **`test_cutex_race`**:
   - 8 runner'ов, множество повторений захвата/освобождения
   - Проверка счетчика результатов (должен быть точный)
   - Пример: `TEST(futures, cutex_race)` в tests.cpp

2. **`test_cutex_race_rescheduling`**:
   - То же, но с `set_rescheduling(true)`
   - Гораздо больше итераций (1M вместо 100K)
   - Проверка оптимизации переноса

### Edge-case тесты:

1. **`test_deadlock_prevention`**:
   - Имитировать "interrupted queue insertion" через delay'ы
   - Убедиться, что `pending_notify()` разрешает deadlock

2. **`test_proxy_raii`**:
   - Использовать proxy без явного `sync()`
   - Убедиться, что деструктор вызывает sync()
   - Проверить exception при неправильном порядке

---

## 8. Структура кода

### Файл: `include/ace/futures/cutex.h`

Должен содержать:

1. **Класс `cutex_future`**:
   - Наследует `future_traits<cutex_future>`
   - Члены: `_users`, `_waiters`, `_runner_pool`, `_rescheduling`
   - Методы: `try_lock()`, `notify()`, `pending_notify()`, `await_ready()`, `await_suspend()`, `await_resume()`
   - Вложенный `struct cutex_conductor`

2. **Класс `cutex`**:
   - Наследует `cutex_future` (protected)
   - Методы: `capture()`, `sync()`
   - Вложенный `class proxy`

3. **Макросы для определений** (уже используются в коде):
   - `#define ACE_FUTURE_CUTEX_FUTURE_SPACE ace::futures::cutex_future::`
   - `#define ACE_FUTURE_CUTEX_MEMBER(returnT)`

4. **Определения методов** (снаружи класса через макросы)

### Namespace:
```cpp
namespace ace::futures { ... }
namespace ace { using futures::cutex; }
```

---

## 9. Ключевые особенности реализации

### 1. MPSC очередь
```cpp
nukes::dynamic::roaming_mpsc_queue<task> _waiters {};
```
- Multi-Producer, Single-Consumer — безопасна для многопоточности
- Динамическая — растет по мере необходимости
- Roaming — может перемещаться между потоками

### 2. Счетчик вместо флага
```cpp
std::atomic<int> _users;  // Не bool!
```
- Используй `int` (не `uint64_t`) чтобы избежать переполнения на вычитание
- Значение 0 = свободен, N > 0 = занят

### 3. Conductor для диспетчеризации
- Критичная часть — conductor **перенаправляет** корутин из runner'а в cutex
- Это избегает busy-polling: runner не будет напрасно пробовать резюме заблокированный корутин

### 4. Цепочка пробуждений через pending_notify()
- Если есть ожидающие, но `notify()` не смог их пробудить немедленно
- Планируется `pending_notify()`, которая в цикле пробует снова
- Это разрешает race-condition deadlock'и

### 5. Rescheduling оптимизация
- Сохраняем пул первого владельца в `_runner_pool`
- При пробуждении передаем пробуждаемому корутину этот же пул
- Если `_rescheduling = false`, каждый корутин сохраняет свой пул

---

## 10. Примеры использования

### Базовое использование:

```cpp
ace::cutex my_mutex;

ace::task task() {
    // Захват
    co_await my_mutex.capture();
    
    // Критическая секция
    shared_data++;
    
    // Освобождение
    my_mutex.sync();
}
```

### Использование proxy (RAII):

```cpp
ace::task safe_task(ace::cutex& mx) {
    ace::croxy proxy(mx);
    
    co_await proxy.capture();
    shared_data++;
    proxy.sync();
    // Или автоматически в деструкторе
}
```

### Многопоточное тестирование:

```cpp
ace::cutex cutex;
std::string counter{"0"};

ace::task racer(int iterations) {
    ace::croxy prx(cutex);
    for (int i = 0; i < iterations; ++i) {
        co_await prx.capture();
        counter = std::to_string(std::stoi(counter) + 1);
        prx.sync();
    }
}

// Запуск с 8 runner'ами
ace::core::s_balancer_config._runners_amount = 8;
ace::reload();
for (int i = 0; i < 8; ++i)
    ace::schedule(racer(100000));
ace::run();
```

---

## 11. Справочные ссылки

### Используемые типы:
- `future_traits<T>` — base для future объектов (include/ace/futures/future.h)
- `task` — тип корутины в фреймворке (include/ace/coroutines/context.h)
- `conductor_handler_t` — интерфейс для диспетчеров (include/ace/coroutines/conductor.h)
- `runner_pool_t` — тип пула runner'а (include/ace/coroutines/context.h)
- `nukes::dynamic::roaming_mpsc_queue<T>` — MPSC очередь (из внешней библиотеки nukes)

### Функции фреймворка:
- `ace::schedule(task&&)` — глобальная планировка (include/ace/core/dispatcher.h)
- `ace::core::runner::reattach()` — возврат корутина в runner
- `ace::suspend()` — приостановка до следующего цикла (type alias)

### Существующие примеры:
- tests/tests.cpp — примеры cutex_race и cutex_race_rescheduling
- tests/units.h — функция racer для понимания использования
