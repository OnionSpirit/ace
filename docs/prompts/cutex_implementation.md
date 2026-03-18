# CUTEX Implementation Prompt

## Project Context

You are implementing a component for the ACE (Asynchronous C++ Execution) framework - a C++20 coroutines library focused on multithreaded execution with lock-free patterns and without system calls.

The project is available at `/home/onions/code/cxx/ace` and involves building an async/await system that dispatches coroutines across multiple runners without blocking syscalls.

---

## Task: Implement CUTEX Mutex Component

### Objective

Implement **cutex** - a userspace mutex built on C++20 coroutines that:
- Works **without syscalls** (no kernel-level locking)
- Functions as a **Future object** compatible with the ACE framework
- Provides **lock acquisition and release** via coroutines
- Uses an **awaiter queue** to avoid busy-polling
- Integrates with the **Conductor pattern** for task dispatching

---

## Architecture Overview

### Key Components to Implement

#### 1. **cutex Class**
The main mutex type implementing `future_traits<cutex>`:

```cpp
class cutex : public future_traits<cutex> {
    // Atomic lock state (true = locked, false = unlocked)
    // Awaiter queue for suspended coroutines
    // Custom conductor for lock handoff
};
```

**Responsibilities:**
- `await_ready()`: Check if lock is free (non-blocking atomic read)
- `await_suspend()`: Set up conductor when lock is contested
- `await_resume()`: Complete lock acquisition

#### 2. **croxy Guard Class**
RAII wrapper for lock acquisition/release:

```cpp
class croxy {
    // Reference to mutex
    // Acquisition state tracking
};
```

**Responsibilities:**
- `capture()`: Acquire lock via `co_await *mutex`
- `sync()`: Release lock and wake next waiter
- `~croxy()`: RAII cleanup (call `sync()`)

#### 3. **cutex_conductor Struct**
Custom conductor implementing `conductor_traits<async<>>`:

```cpp
struct cutex_conductor : conductor_traits<async<>> {
    // Pointer to parent mutex
    void forward(async<>&& ctx) override;
    void cancel() override;
};
```

**Responsibilities:**
- `forward()`: Queue suspended coroutine in awaiter queue
- `cancel()`: Remove from queue on cancellation (complex - see notes)

---

## Conductor Integration (CRITICAL)

### The Problem: Avoiding Busy-Polling

The framework is **async-first** and must never busy-wait. When a coroutine cannot acquire the lock:
1. It suspends via `co_await`
2. The **conductor** receives the suspended coroutine from the runner
3. The conductor stores it in the **awaiter queue**
4. The runner never sees this coroutine again until explicitly reattached

### KEY CONSTRAINT: Runner Reattach as Only Dispatcher

⚠️ **CRITICAL:** The conductor can use **ONLY `runner::reattach()`** to send blocked tasks from its awaiter pool back to the runner's future object. This is the ONLY valid mechanism for task dispatching from the conductor.

This means:
- Conductor's `forward()` receives the suspended context
- Conductor stores it in `_waiters` (awaiter queue)
- Later, when lock is released via `sync()`, we pop from `_waiters`
- We call `runner::reattach()` on the popped context
- Runner adds it back to its ready pool
- Next `runner::yank()` will resume this context

### How Conductor Works in ACE

**From `runner.h` (line 79-93):**
```cpp
const bool is_conducted {
    is_resumable
    and async_n->_data._coroutine.promise()._future_conductor
};

if (is_conducted) [[likely]]
    async_n->_data._coroutine.promise()._future_conductor->forward(
        std::forward<async<>>(async_n->_data)
    );

if (is_idle) _pool.release_node(async_n);
else _pool.push_node(async_n);
```

**What this means:**
- Runner checks `promise()._future_conductor` during `yank()`
- If conductor exists, calls `conductor->forward()` with the suspended context
- The runner **releases the node** - it's no longer in the ready queue
- The conductor's `forward()` must add the context to the awaiter queue
- Context remains suspended until `runner::reattach()` is called

### CUTEX Conductor Pattern

Your conductor must:

1. **In `await_suspend()`:**
   - Create and set the conductor in the promise
   - Return `true` (suspend)

```cpp
bool await_suspend(auto coroutine) {
    coroutine.promise()._future_conductor = cutex_conductor{this};
    return true;
}
```

2. **In conductor's `forward()` - Simple Queue Only:**
   - Just push the context into the awaiter queue
   - Do NOT call `runner::reattach()` here
   - Do NOT try to resume immediately
   - Simply store for later

```cpp
void forward(async<>&& ctx) override {
    _mutex->_waiters.push(std::move(ctx));
    // Context now waits - no reattach() here
}
```

3. **In `sync()` (release lock) - Only Place reattach is Called:**
   - Clear lock state
   - Pop next awaiter from queue
   - Call `runner::reattach()` to move it back to the runner
   - This is the ONLY place `reattach()` is called for CUTEX

```cpp
void sync() {
    _mutex->_locked.store(false, std::memory_order_release);
    if (async<> waiter; _mutex->_waiters.pop(waiter)) {
        core::runner::reattach(std::move(waiter));  // CRITICAL - ONLY HERE
    }
}
```

### Visual Flow

```
┌─────────────────────────────────────────────────────────────────┐
│  Waiter 1 calls co_await mutex                                  │
│  ↓                                                              │
│  await_ready() = false (lock taken)                            │
│  ↓                                                              │
│  await_suspend() sets conductor, returns true                  │
│  ↓                                                              │
│  Runner sees conductor in promise                              │
│  ↓                                                              │
│  Runner calls: conductor->forward(waiter1)                     │
│  ↓                                                              │
│  Conductor calls: _waiters.push(waiter1)  ← No reattach here!  │
│  ↓                                                              │
│  Runner releases node, waiter1 now in _waiters queue           │
│  (NOT in runner ready pool)                                     │
│                                                                 │
│  [Time passes, lock holder doing work...]                      │
│                                                                 │
│  Lock holder calls: guard.sync()                               │
│  ↓                                                              │
│  _locked.store(false, release)                                 │
│  ↓                                                              │
│  Pop waiter1 from _waiters queue                               │
│  ↓                                                              │
│  Call: runner::reattach(waiter1)  ← ONLY HERE!                │
│  ↓                                                              │
│  waiter1 pushed back to runner ready pool                       │
│  ↓                                                              │
│  Next runner::yank() resumes waiter1                            │
│  ↓                                                              │
│  waiter1 wakes up and owns lock                                │
└─────────────────────────────────────────────────────────────────┘
```

---

## Lock State Machine

### Scenario 1: Lock Acquired Immediately

```
Coroutine calls: co_await cutex
  ↓
await_ready(): checks if _locked is false
  ↓
Atomically tries: _locked = compare_exchange(false, true) → true
  ↓
Returns true (lock free)
  ↓
Coroutine does NOT suspend (await_suspend not called)
  ↓
await_resume() executes
  ↓
Coroutine owns lock, continues
```

### Scenario 2: Lock Contested (Suspend and Wait)

```
Coroutine calls: co_await cutex
  ↓
await_ready(): _locked is true
  ↓
Returns false (not ready)
  ↓
await_suspend() is called
  ↓
Creates cutex_conductor and stores in promise
  ↓
Returns true (suspend)
  ↓
Runner calls: conductor->forward(suspended_coroutine)
  ↓
Conductor calls: _waiters.push(suspended_coroutine)
  ↓
Coroutine waits in _waiters queue
  ↓
[Lock holder calls sync()]
  ↓
sync() calls: runner::reattach(next_waiter)
  ↓
next_waiter added back to runner's ready pool
  ↓
Runner's yank() resumes this coroutine
  ↓
await_resume() executes
  ↓
New lock owner continues
```

---

## Implementation Requirements

### 1. Memory Layout and Cache Alignment

Follow the `runner.h` pattern:
- Align `cutex` to cache line boundary
- Place `_locked` as first member (frequently accessed)
- Use `ACE_CACHE_LINE()` macro for padding between fields
- Separate cache lines for `_locked` and `_waiters` to avoid false sharing

```cpp
struct alignas(ACE_CACHE_LINE_SIZE) cutex {
    alignas(ACE_CACHE_LINE_SIZE) std::atomic<bool> _locked{false};
    ACE_CACHE_LINE(0)
    
    alignas(ACE_CACHE_LINE_SIZE)
    nukes::dynamic::mpsc_queue<async<>> _waiters;
};
```

**Why:** 
- Multiple threads access `_locked` frequently
- MPSC queue head/tail updated by waiters
- False sharing would cause cache coherency traffic
- Cache line isolation eliminates contention

### 2. Thread Safety

Use appropriate atomic memory ordering:
- `await_ready()`: Load with `memory_order_acquire` (see if lock is free)
- `await_suspend()` → `forward()`: No special ordering (conductor setup)
- `sync()`: Store with `memory_order_release` (signal lock release)
- Compare-and-swap: Use `compare_exchange_strong()` for robustness

```cpp
bool await_ready() override {
    bool expected = false;
    return _locked.compare_exchange_strong(expected, true);
}
```

### 3. MPSC Queue from Nukes Library

The ACE framework includes the `nukes` library with lock-free queues:
- `nukes::dynamic::mpsc_queue<async<>>` - thread-safe queue for awaiters
- Methods: `push()`, `pop()`, `empty()`
- MPSC = Multiple Producer, Single Consumer (but adapted for our use)

```cpp
#include <nukes/dynamic/mpsc_queue.h>

nukes::dynamic::mpsc_queue<async<>> _waiters;
_waiters.push(std::move(context));
async<> waiter;
if (_waiters.pop(waiter)) {
    // Successfully popped waiter
}
```

### 4. Integration with ACE Macros

Use framework macros for consistency:
- `DECLARE_FUTURE(cutex)` - declares `future_traits_t` 
- `IMPORT_FUTURE_ENV` - imports `derived_future_t` type alias
- Inherit from `future_traits<cutex>` - makes it awaitable

```cpp
class cutex : public future_traits<cutex> {
    DECLARE_FUTURE(cutex)
    IMPORT_FUTURE_ENV
    
    bool await_ready() override;
    bool await_suspend(auto ctx);
    void await_resume() {}
};
```

### 5. Runner Integration

File: `/home/onions/code/cxx/ace/include/ace/core/runner.h`

Know these methods:
- `runner::reattach(async<>&& ctx)` - push context back to runner's pool
- `runner::yank()` - execute one task from ready pool
- `runner::run()` - execute up to 1024 tasks
- `runner::attach(async<&&)` - push new task to runner

Your code must **ONLY** use `runner::reattach()` to send tasks back to the runner from within the conductor.

### 6. Promise Type Integration

Every coroutine has a promise_type with:
- `_future_conductor` - pointer to conductor (set in `await_suspend()`)
- `_runner_pool` - pointer to runner's pool

When you set: `coroutine.promise()._future_conductor = cutex_conductor{...}`

The runner will check this and forward to your conductor's `forward()` method.

---

## Reference Implementations

### Channel Pattern (Most Similar)

File: `/home/onions/code/cxx/ace/include/ace/futures/channel.h`

**Similarities:**
- Uses conductor for async forwarding
- MPSC queue for waiter storage
- `await_ready()` checks condition without blocking
- `await_suspend()` creates conductor if needed
- `forward()` pushes to queue
- `reset()` calls `runner::reattach()` to wake waiters

**Differences:**
- Channel: transmits data, multiple push sources
- CUTEX: transmits ownership, single release source

### Timeout Pattern

File: `/home/onions/code/cxx/ace/include/ace/futures/timeout.h`

**Key insight:**
- Conductor's `forward()` does work beyond just queuing
- In timeout: registers with clock service for delayed resumption
- In CUTEX: should be simpler - just queue the awaiter

---

## Usage Pattern (What Your Code Enables)

```cpp
ace::cutex lock;

ace::async<> worker() {
    ace::croxy guard(lock);
    
    // Attempt to acquire lock (may suspend)
    co_await guard.capture();
    
    // Critical section - we own the lock
    shared_resource++;
    
    // Release lock and wake next waiter
    guard.sync();
    // Or just destroy guard, ~croxy calls sync()
};

// Run in executor
runner.attach(worker());
```

With multiple workers:
- First worker calls `capture()` → `await_ready()` returns true → acquires lock
- Other workers call `capture()` → `await_ready()` returns false → suspend via conductor
- Suspended tasks wait in `_waiters` queue
- First worker calls `sync()` → pops next waiter → calls `runner::reattach()`
- Next waiter resumes in runner on next `yank()`

---

## File Structure and Type Usage

### File Location
**`include/ace/futures/cutex.h`**

### Required Includes
```cpp
#include "future.h"              // future_traits
#include "ace/core/runner.h"     // runner, runner::reattach
#include "ace/coroutines/context.h"  // async<>, conductor_traits
#include "ace/common/terms.h"    // ACE_CACHE_LINE_SIZE, ACE_CACHE_LINE macro
#include <nukes/dynamic/mpsc_queue.h>  // mpsc_queue
#include <atomic>                // std::atomic<bool>
```

### Type Requirements

#### async<> Type
- Full type name: `ace::coroutines::context<>`
- This is the coroutine wrapper type returned by cutex_conductor::forward()
- Has `_coroutine` member of type `coroutine_handle<promise_type>`
- Moved via `std::move()`

#### conductor_traits Type
- Base class for custom conductors
- Methods: `forward(async<>&& ctx)`, `cancel()`
- Virtual destructor

#### MPSC Queue
- `nukes::dynamic::mpsc_queue<async<>>`
- Methods: `push(context)`, `pop(context)` returns bool, `empty()`

#### Memory Ordering Types
- `std::memory_order_acquire` - for reading
- `std::memory_order_release` - for writing  
- `std::memory_order_acq_rel` - for compare_exchange (default)

### Full File Template

```cpp
#ifndef ACE_FUTURE_CUTEX_H
#define ACE_FUTURE_CUTEX_H

#include "future.h"
#include "ace/core/runner.h"
#include "ace/coroutines/context.h"
#include "ace/common/terms.h"
#include <nukes/dynamic/mpsc_queue.h>
#include <atomic>

namespace ace::futures {

// Forward declarations for friend relationships
class cutex;
class croxy;

// Main mutex type - awaitable future
class cutex : public future_traits<cutex> {
    
    struct cutex_conductor;
    friend cutex_conductor;
    
    // Lock state - first member for cache locality
    alignas(ACE_CACHE_LINE_SIZE) std::atomic<bool> _locked{false};
    ACE_CACHE_LINE(0)
    
    // Awaiter queue - separate cache line
    alignas(ACE_CACHE_LINE_SIZE) 
    nukes::dynamic::mpsc_queue<async<>> _waiters;
    
public:
    DECLARE_FUTURE(cutex)
    IMPORT_FUTURE_ENV
    
    cutex() = default;
    ~cutex() override = default;
    
    bool await_ready() override;
    bool await_suspend(auto coroutine);
    void await_resume() {}
};

// Conductor for handling suspended contexts
struct cutex::cutex_conductor : conductor_traits<async<>> {
    cutex* _mutex;
    
    explicit cutex_conductor(cutex* m) : _mutex(m) {}
    
    void forward(async<>&& ctx) override;
    void cancel() override;
    ~cutex_conductor() override = default;
};

// Guard/proxy type for RAII lock management
class croxy {
    cutex* _mutex;
    bool _acquired{false};
    
public:
    explicit croxy(cutex& mutex) : _mutex(&mutex) {}
    ~croxy() { sync(); }
    
    async<> capture();
    void sync();
};

} // namespace ace::futures

// Convenience namespace aliases
namespace ace {
    using cutex = futures::cutex;
    using croxy = futures::croxy;
}

#endif // ACE_FUTURE_CUTEX_H
```

### Type Conversions and Helpers

When working with conductor:
- `async<>&& ctx` - rvalue reference to coroutine context
- `std::move(ctx)` - move context to queue
- `ctx._coroutine.promise()._future_conductor` - access promise field
- `core::runner::reattach(std::move(waiter))` - dispatch back to runner

Note: `core::runner::reattach()` is static method in runner class, use full path.

---

## Critical Points to Remember

1. **No Syscalls:** All operations are userspace - atomic variables and lock-free queues only

2. **Conductor is Essential:** Without proper conductor integration, you'll either:
   - Busy-poll (defeating the purpose of async)
   - Lose track of suspended coroutines
   - Create deadlocks

3. **Runner Reattach Only:** The conductor must use `runner::reattach()` as the ONLY mechanism to move tasks from the conductor's awaiter queue back to the runner's ready pool. The future object always returns tasks through reattach.

4. **Memory Ordering:** Use correct atomic orderings:
   - Acquire when reading
   - Release when writing
   - Full barrier for compare-and-swap

5. **FIFO Fairness:** The MPSC queue ensures waiting coroutines are woken in order - no starvation

6. **Cache Line Padding:** Prevents false sharing in multi-threaded scenarios

7. **RAII Semantics:** The `croxy` guard should ensure `sync()` is always called, even if exception occurs

---

## Testing Guidance

Once implemented, verify with:

```cpp
// Single-threaded lock/unlock
cutex lock;
croxy guard(lock);
co_await guard.capture();
guard.sync();

// Multi-threaded racing (multiple coroutines contending)
// Expected: no data race, fair ordering, correct counts

// Exception safety
// Expected: lock always released, no deadlocks
```

---

## Additional Resources

### Key Files to Study

1. `/home/onions/code/cxx/ace/include/ace/core/runner.h` - understand `reattach()` and `yank()`
2. `/home/onions/code/cxx/ace/include/ace/futures/channel.h` - similar conductor pattern
3. `/home/onions/code/cxx/ace/include/ace/futures/timeout.h` - conductor with custom behavior
4. `/home/onions/code/cxx/ace/include/ace/coroutines/conductor.h` - conductor interface
5. `/home/onions/code/cxx/ace/include/ace/coroutines/context.h` - promise type and async wrapper

### Document References

- `ACE_PATTERNS_FOR_CUTEX.md` - detailed architectural patterns
- `ACE_QUICK_REFERENCE.md` - quick API reference
- `ACE_ARCHITECTURE_ANALYSIS.md` - system design overview

---

## Notes for Implementation

### About `cancel()` in Conductor

The `cancel()` method should handle coroutine cancellation:
- Currently marked TODO in channel and timeout
- Complex: requires finding and removing node from queue
- For initial implementation, can leave as empty with TODO comment
- Future enhancement: implement proper cancellation handling

### About `croxy` Semantics

The guard class provides ergonomic lock management:
- One `croxy` per critical section
- `capture()` is the lock acquire point
- `sync()` is the lock release point
- Destructor ensures cleanup (RAII)

This is similar to `std::scoped_lock` but designed for coroutine suspension.

### About Lock Fairness

The MPSC queue naturally provides FIFO ordering:
- First waiter to suspend is first to wake
- No priority inversion or starvation
- Better than OS mutex under high contention

### About Performance

Expected characteristics:
- **Uncontended:** Single atomic CAS, no allocations, pure userspace
- **Contested:** Coroutine suspends (no busy-loop), awaits in queue
- **Wake:** Next waiter added to runner's ready pool, no extra context switching

---

## Clarifications and Edge Cases

### sync() Implementation Strategy

The `sync()` method in `croxy`:
- Should be callable multiple times safely (idempotent with `_acquired` flag check)
- When called, always sets `_locked = false` first (atomic store with release semantics)
- Then pops one waiter from queue and calls `runner::reattach()`
- Should NOT be called from inside `forward()` - conductor only queues
- MUST work correctly when called from both explicit user code and destructor

### Memory Ordering Semantics

```cpp
// In await_ready():
bool expected = false;
_locked.compare_exchange_strong(expected, true);
// Uses memory_order_acq_rel by default for full synchronization

// In sync():
_locked.store(false, std::memory_order_release);
// Signal to other threads that lock is free

// Reading lock status (channel pattern reference):
// Load with acquire semantics if checking if owned by us
```

### Important: No Modification by croxy Beyond _locked

- `croxy._acquired` is internal flag (not in cutex)
- `croxy` must NOT maintain any state in `cutex` except through atomics
- All waiter queue management is done by conductor and sync()
- `croxy` is lightweight - just guard around mutex operations

### sync() Must Be Idempotent

```cpp
void croxy::sync() {
    if (!_acquired) return;  // Guard against double release
    
    _mutex->_locked.store(false, std::memory_order_release);
    
    if (async<> waiter; _mutex->_waiters.pop(waiter)) {
        core::runner::reattach(std::move(waiter));
    }
    
    _acquired = false;
}
```

This ensures:
- Calling sync() twice doesn't corrupt state
- Destructor can safely call sync() even if already called
- Exception safety maintained

---

## Success Criteria

✅ Implement `cutex` class with proper future_traits inheritance
✅ Implement atomic lock state management with `std::atomic<bool>`
✅ Implement `await_ready()` with compare_exchange for atomic lock acquisition
✅ Implement `await_suspend()` with conductor setup (always returns true)
✅ Implement `croxy` guard with RAII semantics and `_acquired` tracking
✅ Implement `croxy::capture()` as `async<>` coroutine calling `co_await *_mutex`
✅ Implement `croxy::sync()` as idempotent release with `runner::reattach()` call
✅ Implement `cutex_conductor` with proper `forward()` that queues to `_waiters`
✅ Integration with runner via `runner::reattach()` (ONLY mechanism for dispatcher)
✅ Use `nukes::dynamic::mpsc_queue<async<>>` for awaiter storage
✅ Proper cache line alignment with `ACE_CACHE_LINE_SIZE` and `ACE_CACHE_LINE()` macro
✅ Correct atomic memory ordering (acquire/release semantics)
✅ Passes multi-threaded racing tests without data corruption
✅ No busy-polling when lock is contested (full suspension via conductor)
✅ Fair FIFO ordering for waiters (guaranteed by MPSC queue)
✅ Exception-safe via RAII destructor calling `sync()`

---

## Common Implementation Mistakes (Avoid These)

### ❌ Mistake 1: Calling reattach() in conductor::forward()
```cpp
// WRONG - DO NOT DO THIS
void forward(async<>&& ctx) override {
    _mutex->_waiters.push(std::move(ctx));
    runner::reattach(std::move(ctx));  // ❌ WRONG!
}
```

**Why:** This defeats the purpose of the conductor. The context is supposed to wait in the queue, not immediately go back to runner. It will resume too early before lock is actually free.

**Correct:** Only queue in `forward()`. Call `reattach()` only in `sync()`.

---

### ❌ Mistake 2: Not returning true from await_suspend()
```cpp
// WRONG
bool await_suspend(auto coroutine) {
    coroutine.promise()._future_conductor = cutex_conductor{this};
    return false;  // ❌ WRONG - don't suspend?
}
```

**Why:** Returning false means "don't suspend". But we set a conductor specifically to handle suspension. If we return false, the conductor never gets called by runner.

**Correct:** Always return `true` in await_suspend() when conductor is set.

---

### ❌ Mistake 3: Modifying lock state in wrong place
```cpp
// WRONG - setting lock free in forward()
void forward(async<>&& ctx) override {
    _mutex->_locked.store(false);  // ❌ WRONG!
    _mutex->_waiters.push(std::move(ctx));
}
```

**Why:** Lock state should only change in `await_ready()` (acquire) and `sync()` (release). Changing it here breaks the lock semantics. Next waiter might think it's free.

**Correct:** Lock state only in `await_ready()` and `sync()`.

---

### ❌ Mistake 4: Not handling race in sync()
```cpp
// WRONG - no existence check
void sync() {
    _mutex->_locked.store(false);
    async<> waiter;
    _mutex->_waiters.pop(waiter);  // ❌ If pop returns false, waiter is uninitialized
    runner::reattach(std::move(waiter));
}
```

**Why:** `pop()` returns bool. If false, queue was empty. We must check.

**Correct:**
```cpp
void sync() {
    if (!_acquired) return;  // Guard
    _mutex->_locked.store(false, std::memory_order_release);
    if (async<> waiter; _mutex->_waiters.pop(waiter)) {
        core::runner::reattach(std::move(waiter));
    }
    _acquired = false;
}
```

---

### ❌ Mistake 5: Using wrong queue type
```cpp
// WRONG - MPMC instead of MPSC
nukes::dynamic::mpmc_queue<async<>> _waiters;  // ❌ Wrong!
```

**Why:** MPMC = Multiple Producer, Multiple Consumer. We need MPSC = Multiple Producer (multiple waiters pushing), Single Consumer (single release popping). MPMC adds unnecessary synchronization overhead.

**Correct:** `nukes::dynamic::mpsc_queue<async<>>`

---

### ❌ Mistake 6: Not considering memory ordering
```cpp
// WRONG - no memory ordering
_locked.store(false);  // ❌ Unspecified ordering!
```

**Why:** Lock releases must use release semantics to ensure all writes before release are visible to next acquirer.

**Correct:**
```cpp
_locked.store(false, std::memory_order_release);
```

---

### ❌ Mistake 7: Conductor not inheriting from conductor_traits
```cpp
// WRONG - just a regular struct
struct cutex_conductor {  // ❌ Missing base class
    void forward(async<>&& ctx) override { }
}
```

**Why:** Runner uses `conductor_traits` interface to call `forward()`. Without proper inheritance, runner won't recognize it.

**Correct:**
```cpp
struct cutex_conductor : conductor_traits<async<>> {
    void forward(async<>&& ctx) override { }
    void cancel() override { }
    ~cutex_conductor() override = default;
}
```

---

### ❌ Mistake 8: Creating conductor on stack with dangling pointer
```cpp
// WRONG - conductor has pointer to local
bool await_suspend(auto coroutine) {
    coroutine.promise()._future_conductor = 
        std::make_shared<cutex_conductor>(this);  // ❌ Shared ownership confusion
    return true;
}
```

**Why:** Promise expects raw pointer, and conductor can be lightweight. Also shared_ptr adds overhead.

**Correct:**
```cpp
bool await_suspend(auto coroutine) {
    coroutine.promise()._future_conductor = cutex_conductor{this};
    return true;
}
```

---

### ❌ Mistake 9: Not including all required headers
```cpp
// Missing includes - won't compile
#include "future.h"  // ✓
// Missing runner.h, context.h, atomic, etc
```

**Why:** Compilation will fail with missing type errors.

**Correct:** Include all headers from File Structure section.

---

### ✅ Verification Checklist

Before considering implementation complete:

- [ ] `cutex` inherits from `future_traits<cutex>`
- [ ] `cutex` uses `std::atomic<bool>` for lock state
- [ ] `await_ready()` uses `compare_exchange_strong()` 
- [ ] `await_suspend()` always returns `true`
- [ ] `await_suspend()` creates `cutex_conductor` and stores in promise
- [ ] `croxy::capture()` returns `async<>` coroutine
- [ ] `croxy::sync()` is idempotent with `_acquired` check
- [ ] `croxy::~croxy()` calls `sync()`
- [ ] `cutex_conductor::forward()` only pushes to queue (no reattach)
- [ ] `cutex_conductor::forward()` does not modify `_locked`
- [ ] Lock release (sync) is ONLY place calling `runner::reattach()`
- [ ] Using `nukes::dynamic::mpsc_queue<async<>>`
- [ ] Memory aligned to cache lines with `ACE_CACHE_LINE_SIZE`
- [ ] Atomic operations use correct memory ordering
- [ ] All headers included
- [ ] Namespace organization (futures:: and ace::)

---

## Questions for Clarification

If you have questions about:
- Conductor behavior: reference `channel.h` pull_impl::await_suspend()
- Memory ordering: reference atomic semantics in modern C++
- MPSC queue usage: reference nukes library documentation
- Promise integration: reference runner::yank() implementation
- RAII patterns: standard C++ practice

Ask before implementing to avoid rework.
