# ACE Project Architecture and Design Analysis

## 1. OVERALL PROJECT STRUCTURE

### Main Components
The ACE project is a C++20 coroutine-based async execution framework organized into several key modules:

**Directory Structure:**
```
/home/onions/code/cxx/ace/
├── include/ace/
│   ├── ace.h                 # Main entry point
│   ├── common/               # Common utilities and concepts
│   ├── core/                 # Core execution infrastructure
│   ├── coroutines/          # Coroutine context and promise system
│   ├── futures/             # Future-like async types
│   ├── commands/            # Command pattern for async operations
│   └── oldbase/             # Legacy/deprecated components
├── subprojects/nukes/       # Lock-free queue implementations (MPSC/MPMC)
├── tests/                   # Unit tests
└── build/                   # Build artifacts
```

### Main Entry Point
- **ace.h**: Minimal includes with aliases for quick start
  - Includes `coroutines/context.h` and `core/dispatcher.h`

---

## 2. FUTURES IMPLEMENTATION

### Future Architecture (`include/ace/futures/`)

**Core Future Hierarchy:**
```cpp
future_handler (abstract base)
    ↓
future_traits<T> (CRTP base)
    ↓
Concrete types: channel, timeout, join_handler, etc.
```

**Key Files:**

#### `future.h`
- **future_handler**: Abstract base with `await_ready()` virtual method
- **future_traits<T>**: CRTP base providing:
  - `operator co_await()` - overloaded for copy/move constructibility
  - Makes derived class awaitable with `co_await`
- Macros:
  - `DECLARE_FUTURE(T)` - creates `future_traits_t` typedef
  - `IMPORT_FUTURE_ENV` - imports `derived_future_t` type

**Awaitable Concept:**
```cpp
concept is_awaitable<T, Promise> {
    bool await_ready();
    T& await_resume();
    std::coroutine_handle | bool | void await_suspend(coroutine_handle<Promise>);
}

concept is_future<T, Promise> {
    requires { typename T::future_traits_t; }
    && std::derived_from<T, future_traits_t>
    && is_awaitable<T, Promise>;
}
```

#### `channel.h`
- **Generic Channel**: Multi-producer multi-consumer async channel
- Template parameters:
  - `data_t`: Type of data to transmit
  - `data_buffer_size_v`: Data buffer size
  - `data_allocation_v`: Allocation policy (e_static, e_on_init, e_dynamic)
  - `waiters_buffer_size_v`: Waiting contexts buffer size
  - `waiters_allocation_v`: Waiters allocation policy

- **Components**:
  - `data_storage_t`: Queue holding transmitted data
  - `waiters_storage_t`: Queue holding waiting coroutines
  - `pull_impl`: Future implementation for async pull operation
  - `channel_conductor`: Conductor for handling forwarded contexts

- **Operations**:
  - `push(data)`: Add data, wake up one waiter
  - `pull()`: Async wait for data
  - Alternative operators: `<<` (push), `>>` (pull)

- **State Machine**:
  ```
  push() → check if data available → wake waiting coroutine → reattach to runner
  pull() → check if data ready → if not, suspend with conductor → conductor resumes on push
  ```

#### `timeout.h`
- **timeout**: Future that suspends for specified duration
- **expire**: Variant that suspends until absolute timepoint
- Uses `core::clock` for time management
- **timeout_conductor**: Subscribes with clock and marks as released when expired
- Clock node injection allows delayed resumption via clock service

#### `async_handle.h`
- **join_handler**: Future for waiting on spawned tasks
- **async_handle**: Public interface for task observation
- Provides: `join()`, `done()`, `cancel()` operations
- **join_handler_conductor**: Forwards waiting contexts to target coroutine

### Synchronization between Futures and Runner

**Key Mechanism: Conductor**
```
Future decides to suspend context
    ↓
Creates conductor_traits<runner_context_t> derivative
    ↓
Stores in promise._future_conductor
    ↓
Runner checks for conductor on resume
    ↓
If conductor exists: forward context via conductor
    ↓
Otherwise: push back to runner pool
```

---

## 3. CONDUCTOR AND RUNNER ARCHITECTURE

### Conductor System (`include/ace/coroutines/conductor.h`)

**conductor_traits<runner_context_t>**:
- Abstract interface for forwarding contexts
- Methods:
  - `forward(runner_context_t&& context)`: Forward context to storage
  - `cancel()`: Cancel pending operation

**Design Purpose:**
- Decouples futures from runner
- Allows futures to specify alternative storage/handling
- Enables nested coroutine forwarding (outer context passes conductor to inner)

### Runner System (`include/ace/core/runner.h`)

**runner Class**:
```cpp
struct alignas(ACE_CACHE_LINE_SIZE) runner {
    mutable runner_pool_t _pool;  // Task queue
    
    // Core methods:
    bool yank()              // Resume one ready task
    std::optional<async<>> eject()  // Remove task
    bool run()               // Resume up to 1024 ready tasks
    void attach<T>(async<T>&&)      // Add task to runner
    bool empty() const       // Check if empty
    static void reattach(async<>&&) // Return task to source runner
};
```

**Execution Flow**:
```
yank():
    1. Pop task from pool
    2. If no task → return false
    3. Call awake(&touch_result) to resume
    4. Check if resumable:
       - is_resumable ← true if not done/failed/finished/detached
    5. Check for conductor:
       - is_conducted ← has _future_conductor set
    6. Decision:
       - If conducted: forward via conductor, release node
       - If resumable but not conducted: push back to pool
       - If not resumable: release node
    7. Return true if processed

run():
    1. Call yank() up to 1024 times
    2. Return true if made any progress
```

**Task State Management**:
- Tasks stored in `runner_pool_t` (MPSC queue)
- `_future_conductor`: Set by future when suspending
- `_runner_pool`: Back-pointer to owning runner
- `_roaming`: Flag for dispatcher-scheduled tasks

### Context/Promise System (`include/ace/coroutines/`)

#### `context.h` - Main Coroutine Type

```cpp
template<typename returnT = void, is_promise_rule promise_rule_t = differed>
struct context : futures::future_traits<context<returnT, promise_rule_t>> {
    
    struct promise_type;
    
    coroutine_t _coroutine;
    
    // Methods:
    bool is_resumable() const
    bool await_ready() override
    bool await_suspend(coroutine_handle outer)
    returnT await_resume()
    returnT awake(promise_touch_result* res = nullptr)
    control_block_handle observe()
    void release_future()
    void release_waiters()
};
```

**Promise Rules:**
- `permanent`: `std::suspend_never` - runs immediately (eager)
- `differed`: `std::suspend_always` - suspends initially (lazy)

**Type Aliases:**
- `ace::async<T>`: Lazy coroutine (differed) - default
- `ace::promise<T>`: Eager coroutine (permanent)

#### `promise.h` - Promise Traits

```cpp
template<typename returnT>
struct promise_traits : promise_return_traits<...> {
    
    future_handler_ptr_t _future { nullptr }
    control_block* _block { nullptr }
    std::optional<std::size_t> _trace_id
    
    // Transform co_await expressions:
    futureT& await_transform(futureT& future)
    futureT&& await_transform(futureT&& future)
    commandT& await_transform(commandT& command)
    commandT&& await_transform(commandT&& command)
    
    // Memory management:
    void* operator new(size_t)      // Allocates control block prefix
    void operator delete(void*)     // Smart cleanup
    
    // Tracing:
    std::size_t setup_trace()
};
```

**Return Value Handling:**
- `return_value(T)`: Sets `_return_value`, returns `suspend_never`
- `yield_value(T)`: Sets `_return_value`, returns `suspend_always`
- `return_void()`: Only for `void` specialization

#### `control.h` - Control Block System

```cpp
struct control_block {
    uint64_t _weak_refcount {1}
    uint64_t _strong_refcount {1}
    promise_conductor_handle* _promise_conductor { nullptr }
    bool _exists {true}
    
    static methods for reference counting:
    - is_untracked(v_block)
    - disown(v_block)
    - watch(v_block)
    - unwatch(v_block)
    - is_disowned(address)
    - get_block_from_address(address)
};

class control_block_handle {
    control_block* _block
    
    // Operations:
    void cancel()
    bool done() const
    bool is_idle() const
    bool forward(void* waiter) const
};
```

**Allocation Strategy:**
- Control block allocated BEFORE promise in custom `operator new`
- Address of promise = base_ptr + control_block_size
- Retrieved from promise address by subtracting control_block_size

**Reference Counting:**
- _weak_refcount: Number of observers (handles)
- _strong_refcount: Owner count
- Deletion only when both reach 0

### Context Conductor (`context.h` - nested class)

```cpp
class context::promise_conductor : public promise_conductor_handle {
    void* _address { nullptr }
    
    void cancel() override
    bool forward(void* undefined_waiter) override
};
```

**Purpose**: Allows outer context to manage inner context suspension state

**conductor_carry** (nested in context):
- Type-safe storage for conductor instances
- Allocated on stack in promise
- Size bounded by `ACE_CONDUCTOR_MEM_SIZE`
- Supports assignment and move operations
- Provides `release()` for destruction and `reset()` for clearing

---

## 4. DISPATCHER AND SCHEDULING

### Dispatcher (`include/ace/core/dispatcher.h`)

**dispatcher Class** (Singleton):
```cpp
class dispatcher {
    balancer _balancer
    sig_pipe_t _sig_pipe
    
    static dispatcher& get_instance()
    void schedule(async<>&&, const runner* rnr = nullptr)
    bool empty() const
    void run()
    void reload()
};
```

**Global API**:
```cpp
namespace ace {
    void schedule(async<>&&, const runner* rnr = nullptr)
    void run()
    bool empty()
    void reload()
    void interrupt()
    void terminate()
    void reset_signal()
}
```

**Scheduling Process**:
1. Task marked with `_roaming = true`
2. Passed to balancer for distribution
3. Balancer selects runner
4. Runner executes tasks via `yank()` in loop

### Balancer (`include/ace/core/balancer.h`)

**balancer Class**:
```cpp
class balancer {
    std::vector<runner> _runners
    balancer_config _balancer_config
    std::vector<worker_state> _workers_states
    std::atomic<std::size_t> _runner_selector
    
    void worker_round(int worker_id)
    void worker_tf(const std::stop_token&, int worker_id)
    void schedule(async<>&&, const runner* rnr = nullptr)
    bool reload()
    bool empty() const
};
```

**Configuration**:
- `balancer_config::_runners_amount`: Number of worker threads
- Configurable via global `ace::core::s_balancer_config`
- Runtime reload if queue is empty

**Execution Strategy**:
- Multiple threads, one per runner
- Each thread runs worker loop:
  1. Execute up to 1M iterations of `runner.run()`
  2. If active, continue spinning
  3. If idle, sleep 1ms
- Load balancing via round-robin task distribution

### Clock Service (`include/ace/core/clock.h`)

**clock_record**:
- Stores context + duration
- Thread-local slab memory pool

**time_slot**:
- Stores records expiring at same time
- `release_record()`: Calls `runner::reattach()`

**Global Clock Functions**:
- `clock_now()`: Current steady_clock time
- `subscribe(context, duration)`: Subscribe to timeout
- `detach(clock_node*)`: Unsubscribe

---

## 5. COMMANDS SYSTEM

### Command Architecture (`include/ace/commands/`)

**command_traits<T>** (similar to future_traits):
```cpp
template<typename derivedT>
struct command_traits : future_handler {
    auto&& operator co_await()
    bool await_ready() override { return false; }
};

concept is_command<T, Promise> {
    requires { typename T::command_traits_t; }
    && derived_from<T, command_traits_t>
    && is_awaitable<T, Promise>;
}
```

**Macros**:
- `DECLARE_COMMAND(T)`: Creates `command_traits_t`
- `IMPORT_COMMAND_ENV`: Imports `derived_command_t`

### Key Commands

#### `spawn.h`
- **spawn**: Command to spawn task on same runner
```cpp
class spawn : public command_traits<spawn> {
    async<> _task
    control_block_handle _handle
    
    bool await_suspend(auto coroutine)
    futures::async_handle await_resume() const
};
```
- Usage: `co_await ace::spawn(task)` → returns `async_handle`

#### `get_runner.h`
- **get_runner**: Command to get current runner pointer
- Usage: `auto runner_ptr = co_await ace::commands::get_runner()`

#### `reattach.h`
- Reattach task to its source runner

---

## 6. SYNCHRONIZATION PRIMITIVES

### Existing Components (Oldbase - Deprecated)

**lock.h** (riot namespace - legacy):
- Template-based lock with strategies
- Supports `co_await` operator
- Primitives: atomic, mutex
- Strategies: ordered, unordered

### New Implementation: CUTEX (Coroutine Mutex)

**Usage Pattern (from tests)**:
```cpp
ace::async<> racer(const int& max, std::string& shared_counter, ace::cutex& cut) {
    ace::croxy crx(cut);  // Proxy object
    for (volatile int i = 0; i < max; i = i + 1) {
        co_await crx.capture();  // Acquire lock
        shared_counter = std::to_string(std::stoi(shared_counter) + 1);
        crx.sync();  // Release lock
    }
    co_await crx.capture();
    std::cout << "'racer' finished\n";
}
```

**Expected Types**:
- `ace::cutex`: Main mutex type
- `ace::croxy`: Proxy/guard type for RAII-like behavior

**Test Requirements**:
- `cutex_race`: Concurrent access by 8 runners, 100k iterations each
- `cutex_race_resheduling`: Advanced rescheduling scenarios

---

## 7. CODE PATTERNS AND CONVENTIONS

### Memory Alignment
```cpp
#define ACE_BUS_SIZE sizeof(std::size_t)
#define ACE_CACHE_LINE_SIZE std::hardware_constructive_interference_size
#define ACE_CONDUCTOR_MEM_SIZE std::hardware_constructive_interference_size - ACE_BUS_SIZE

struct alignas(ACE_CACHE_LINE_SIZE) runner {
    ACE_CACHE_LINE(0)  // Padding macro
    mutable runner_pool_t _pool;  // Must be first member
};
```

### Type Traits and Concepts
- Heavy use of `requires` clauses
- CRTP pattern for future_traits and command_traits
- Concept-based dispatch in promise's `await_transform`

### Move Semantics
- Extensive use of rvalue references and move semantics
- Coroutine handles and async<> are move-only types
- Temporary futures are rvalue-bound to `&&` parameters

### Awaitable Pattern
```cpp
struct my_future : future_traits<my_future> {
    DECLARE_FUTURE(my_future)
    IMPORT_FUTURE_ENV
    
    bool await_ready() override { }
    template<typename promise_u>
    bool await_suspend(std::coroutine_handle<promise_u> outer) { }
    auto await_resume() { }
};
```

### Conductor Pattern
```cpp
struct my_conductor : conductor_traits<async<>> {
    void forward(async<>&& ctx) override { 
        // Store context somewhere
    }
    void cancel() override { 
        // Cancel operation
    }
};

// In future:
bool await_suspend(auto coroutine) {
    coroutine.promise()._future_conductor = my_conductor{...};
    return true;  // Suspend
}
```

### MPSC/MPMC Queues
- Uses `nukes` subproject lock-free queues
- Dynamic allocation: `nukes::dynamic::mpsc_queue<T>`
- Static allocation: `nukes::bounded::mpsc_queue<T, Size>`
- Thread-safe multi-producer scenarios

### Allocators and Pools
- `slab_mempool<T>`: Pre-allocated chunk-based memory pool
- `queue<T>`: Linked-list queue with node pooling
- Clock records use thread-local memory pools

---

## 8. KEY FILES TO REFERENCE FOR CUTEX IMPLEMENTATION

### Must Read
1. **`include/ace/futures/channel.h`**
   - Most complete example of conductor pattern
   - Shows how to handle waiting contexts
   - Demonstrates integration with runner

2. **`include/ace/core/runner.h`**
   - Task execution model
   - Conductor checking and forwarding
   - Reference counting through promise._runner_pool

3. **`include/ace/coroutines/conductor.h`**
   - Conductor interface contract
   - Base for custom conductors

4. **`include/ace/coroutines/context.h`**
   - Promise type with conductor_carry
   - Context storage in runners
   - Waiter management (if needed)

5. **`include/ace/coroutines/promise.h`**
   - Promise traits implementation
   - await_transform dispatching
   - Memory allocation strategy

### Reference
6. **`include/ace/futures/timeout.h`**
   - Alternative conductor usage pattern
   - Clock integration example

7. **`include/ace/futures/async_handle.h`**
   - Join handler conductor
   - Alternative forwarding mechanism

8. **`include/ace/commands/spawn.h`**
   - Command pattern for sync operations
   - Reference to runner pool

### Test Patterns
9. **`tests/units.h`**
   - `racer()` function showing CUTEX usage
   - `croxy` type pattern
   - Expected test scenarios

---

## 9. DESIGN PRINCIPLES OBSERVED

### 1. Zero-Copy Task Forwarding
- Contexts moved between queues, never copied
- Conductors specify destination queue
- Runner checks conductor before requeuing

### 2. Cache-Line Alignment
- Critical structures aligned to cache lines
- Reduces false sharing in multi-threaded scenarios
- Pool is always first member (offset 0)

### 3. CRTP for Type Safety
- future_traits<T>, command_traits<T>
- Enables operator co_await at compile time
- No virtual dispatch overhead in awaitable operations

### 4. Lazy Evaluation by Default
- `ace::async<>` uses `differed` (suspend_always)
- `ace::promise<>` uses `permanent` (suspend_never)
- Allows both eager and lazy patterns

### 5. Conductor Pattern
- Elegant decoupling of suspend/resume logic
- Future specifies storage location, not runner details
- Enables complex chaining (nested contexts)

### 6. Thread Safety via Lock-Free Queues
- MPSC/MPMC from nukes subproject
- No mutex contention in happy path
- Scalable multi-runner architecture

### 7. Composable Futures
- Channels, timeouts, joins all compose cleanly
- Each implements future_traits pattern
- Can be nested and awaited in any order

---

## 10. ARCHITECTURE DIAGRAM

```
┌─────────────────────────────────────────────────────────────┐
│                     ace::dispatcher (Singleton)             │
│  - Global scheduling point                                  │
│  - Maintains balancer                                       │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ↓
        ┌──────────────────────────────┐
        │      ace::balancer           │
        │  - Multiple runners          │
        │  - Round-robin distribution  │
        │  - Worker threads            │
        └──────────────────────────────┘
                  │    │    │
        ┌─────────┘    │    └─────────┐
        ↓              ↓              ↓
    ┌────────┐    ┌────────┐    ┌────────┐
    │ runner │    │ runner │    │ runner │
    │ (pool) │    │ (pool) │    │ (pool) │
    └────────┘    └────────┘    └────────┘
        ↑              ↑              ↑
        │              │              │
    ┌───┴──────────────┴──────────────┴───┐
    │                                      │
    │  async<> contexts (coroutines)      │
    │  - promise_type with conductor      │
    │  - _future_conductor carrier        │
    │  - _runner_pool back-pointer        │
    │  - control_block for lifetime       │
    └──────────────────────────────────────┘
         ↑              ↑              ↑
         │ awaits       │ awaits       │
    ┌────┴────┐    ┌────┴────┐   ┌──────────┐
    │ channel │    │ timeout │   │ async_   │
    │         │    │ /expire │   │ handle   │
    │         │    │         │   │          │
    │conductr │    │conductr │   │conductr  │
    └─────────┘    └─────────┘   └──────────┘
         │ stores     │ registers   │ observes
         │ context    │ with clock  │ target
         └────────────┴─────────────┘
                      ↓
           [Conductor forwards to
            appropriate queue/system]
```

---

## 11. IMPLEMENTATION ROADMAP FOR CUTEX

Based on the architecture analysis, CUTEX should:

1. **Implement core types**:
   - `cutex`: Main mutex type
   - `croxy`: Proxy/guard type
   - Custom conductor for lock management

2. **Use conductor pattern**:
   - Store waiting contexts in queue
   - Forward on unlock via conductor
   - Similar to channel pattern

3. **Integrate with existing infrastructure**:
   - Use runner pool for context storage
   - Leverage MPSC queues for waiter tracking
   - Follow promise_traits patterns

4. **Handle concurrent access**:
   - Lock-free queue for waiters
   - Atomic flag for lock state
   - Order-preserving FIFO for fairness

5. **Test requirements** (from tests.cpp):
   - Support multiple concurrent racers
   - Handle 100k+ iterations
   - Work with dispatcher/balancer
   - Support rescheduling scenarios

