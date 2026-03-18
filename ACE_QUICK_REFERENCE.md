# ACE Project - Quick Reference Guide

## Component Summary Table

| Component | File | Purpose | Key Types |
|-----------|------|---------|-----------|
| **Futures** | `futures/future.h` | Base classes for all awaitable types | `future_handler`, `future_traits<T>` |
| **Channel** | `futures/channel.h` | MPMC async data queue | `channel<T>`, `pull_impl` |
| **Timeout** | `futures/timeout.h` | Time-based suspension | `timeout`, `expire` |
| **Async Handle** | `futures/async_handle.h` | Join and observe spawned tasks | `async_handle`, `join_handler` |
| **Context/Async** | `coroutines/context.h` | Main coroutine type | `async<T>`, `promise<T>` |
| **Promise** | `coroutines/promise.h` | Promise type implementation | `promise_traits<T>` |
| **Conductor** | `coroutines/conductor.h` | Forward context between queues | `conductor_traits<T>` |
| **Control** | `coroutines/control.h` | Reference counting for contexts | `control_block`, `control_block_handle` |
| **Runner** | `core/runner.h` | Execute tasks from queue | `runner` |
| **Dispatcher** | `core/dispatcher.h` | Global task scheduling | `dispatcher` |
| **Balancer** | `core/balancer.h` | Multi-runner task distribution | `balancer` |
| **Clock** | `core/clock.h` | Time management service | `clock`, `clock_record` |
| **Commands** | `commands/command.h` | Base for command pattern | `command_traits<T>` |
| **Spawn** | `commands/spawn.h` | Spawn task on same runner | `spawn` |

---

## Type Relationships

```
awaitable_concept
├── future_traits<T>
│   ├── context<T, rule>
│   ├── channel<T, ...>::pull_impl
│   ├── timeout / expire
│   └── async_handle / join_handler
└── command_traits<T>
    ├── spawn
    └── get_runner
```

---

## Memory Layout: Promise Allocation

```
[control_block_size bytes]  <- control_block
[promise_type data]         <- promise
    ├── _return_value
    ├── _status
    ├── _future (future_handler_ptr_t)
    ├── _block (control_block*)
    ├── _trace_id (optional<size_t>)
    ├── _future_conductor (conductor_carry)
    ├── _runner_pool (runner_pool_t*)
    ├── _waiters (atomic<shared_ptr<runner_pool_t>>)
    ├── _promise_conductor (optional<promise_conductor>)
    └── _roaming (bool)
```

---

## Context Lifecycle

```
Created (co_return starts)
    ↓
initial_suspend() → returns suspend_rule (suspend_always or suspend_never)
    ↓
[Coroutine body executes or suspends]
    ↓
When awaiting future:
    - await_ready() checked
    - If false: await_suspend() called with conductor setup
    - Future stored in promise._future
    - Runner checks for conductor on yank()
    ↓
final_suspend() → suspends always
    ↓
Destroyed (destructor called)
```

---

## Conductor Flow

### Channel Example
```
1. Coroutine: co_await channel.pull()
2. pull_impl::await_suspend() runs:
   - If data ready: return false (don't suspend)
   - If data not ready:
     - Create channel_conductor (stores _waiters pointer)
     - Assign to outer.promise()._future_conductor
     - return true (suspend)
3. Runner checks yank():
   - is_conducted = promise._future_conductor is set
   - forward() called: channel_conductor::forward(context)
   - Pushes context to _waiters queue
4. When push() called:
   - Data added
   - reset() called: pops waiter and calls runner::reattach()
   - Waiter resumed on next runner cycle
```

---

## Task State Transitions

```
┌─────────────┐
│   Created   │
└──────┬──────┘
       │ attach()
       ↓
┌──────────────┐    (runner._pool)
│    Ready     │
└──────┬───────┘
       │ yank()
       ↓
┌──────────────┐
│   Running    │ ← resume() called
└──────┬───────┘
       │
       ├─ finished ──→ Release
       ├─ failed ────→ Release
       ├─ detached ──→ Release
       └─ suspended ─→ Check conductor
                      ├─ Has conductor ──→ forward() → Different queue
                      └─ No conductor ───→ Push back to pool
```

---

## Synchronization Primitives Comparison

| Type | Status | Threading | Pattern | Example |
|------|--------|-----------|---------|---------|
| **lock** (oldbase) | Deprecated | Any | Co-await lock | old API |
| **channel** | Active | Multi-threaded | Conductor-based | Data passing |
| **timeout** | Active | Any | Clock-based | Sleep/delay |
| **join_handler** | Active | Any | Observer pattern | Wait for task |
| **cutex** | TBD | Multi-threaded | Mutex pattern | **TO IMPLEMENT** |

---

## Key Macros

| Macro | Definition | Usage |
|-------|-----------|-------|
| `ACE_BUS_SIZE` | `sizeof(std::size_t)` | Pointer size |
| `ACE_CACHE_LINE_SIZE` | `std::hardware_constructive_interference_size` | Cache line for alignment |
| `ACE_CONDUCTOR_MEM_SIZE` | `ACE_CACHE_LINE_SIZE - ACE_BUS_SIZE` | Conductor storage |
| `DECLARE_FUTURE(T)` | Creates `future_traits_t` typedef | In future types |
| `IMPORT_FUTURE_ENV` | Imports `derived_future_t` | In future types |
| `DECLARE_COMMAND(T)` | Creates `command_traits_t` typedef | In command types |
| `IMPORT_COMMAND_ENV` | Imports `derived_command_t` | In command types |

---

## Queue Types (from nukes)

```
nukes::dynamic::mpsc_queue<T>        // Multi-producer, single-consumer, dynamic
nukes::dynamic::mpmc_queue<T>        // Multi-producer, multi-consumer, dynamic
nukes::bounded::mpsc_queue<T, Size>  // Static sized MPSC
nukes::bounded::mpmc_queue<T, Size>  // Static sized MPMC
```

---

## API Quick Start

### Creating and Running Tasks

```cpp
// Simple coroutine
ace::async<int> my_task() {
    co_return 42;
}

// Spawn and run
int main() {
    ace::schedule(my_task());  // Schedule on dispatcher
    ace::run();                 // Run until empty
}
```

### Using Channels

```cpp
ace::futures::channel_dyn<int> ch;

ace::async<> producer() {
    ch.push(42);
}

ace::async<> consumer() {
    int value = co_await ch.pull();
}
```

### Using Timeouts

```cpp
ace::async<> sleeper() {
    co_await ace::futures::timeout(100ms);
    // Resumed after 100ms
}
```

### Spawning and Joining

```cpp
ace::async<> parent() {
    auto handle = co_await ace::spawn(child_task());
    if (co_await handle.join()) {
        // Child completed
    }
}
```

---

## Testing Checklist

- [ ] Single-threaded basic execution (context.h)
- [ ] Multi-threaded context racing
- [ ] Channel push/pop across threads
- [ ] Timeout/clock integration
- [ ] Join handler and cancellation
- [ ] Conductor forwarding
- [ ] Memory cleanup (no leaks)
- [ ] Fair FIFO ordering
- [ ] Exception safety

---

## Performance Considerations

1. **Cache Alignment**: Use `alignas(ACE_CACHE_LINE_SIZE)` for frequently modified data
2. **Lock-Free**: Use nukes queues for thread-safe operations without locks
3. **Move Semantics**: Always move coroutines and tasks, never copy
4. **Lazy Evaluation**: Default to `async<>` which suspends initially
5. **Conductor Pattern**: Minimize copying by forwarding pointers
6. **Memory Pools**: Use slab allocation for frequent small allocations
7. **Thread Pinning**: Each runner runs on dedicated thread, no migration

---

## Common Pitfalls

1. **Holding Conductors After Scope**: Conductor_carry destroyed at scope exit
2. **Copying Coroutines**: Use move semantics only
3. **Blocking in Promise**: Blocks entire runner thread
4. **Not Awaiting Futures**: Future without co_await is just object
5. **Nested Co_Await Without Conductor**: Inner suspend not visible to runner
6. **Forgetting Runner Pool**: Context loses execution if pool pointer lost
7. **Not Handling Roaming**: Dispatcher-scheduled tasks need proper setup

---

## Debugging Tips

1. Check `_coroutine.done()` to see if context is finished
2. Check `_future_conductor` to track suspension reason
3. Use `observe()` to create control_block_handle for external tracking
4. Check `_roaming` flag to see if task is dispatcher-scheduled
5. Monitor runner pool size with `empty()` method
6. Use tracing with `setup_trace()` for IDs

