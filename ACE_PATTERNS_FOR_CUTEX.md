# ACE Patterns Applied to CUTEX Implementation

## 1. CUTEX Architecture Overview

Based on the channel and timeout implementations, CUTEX should follow this pattern:

```cpp
// Header: include/ace/futures/cutex.h

namespace ace::futures {
    
    // Main mutex type
    class cutex : public future_traits<cutex> {
        // Lock state management
        // Waiter queue management
        // Conductor creation on suspend
    };
    
    // Proxy/Guard type for RAII semantics
    class croxy {
        cutex* _mutex;
        // acquire() and release() semantics
    };
    
    // Custom conductor for lock handoff
    struct cutex_conductor : conductor_traits<async<>> {
        // Forward context to next waiter
    };
}

// Namespace alias
namespace ace {
    using cutex = futures::cutex;
    using croxy = futures::croxy;
}
```

---

## 2. Conductor Pattern Application for CUTEX

### Channel Pattern (Reference)
```cpp
struct channel_conductor : conductor_traits<async<>> {
    waiters_storage_t* _waiters;
    
    void forward(async<>&& ctx) override { 
        _waiters->push(std::move(ctx));  // Store context
    }
};

// In pull_impl::await_suspend():
ctx.promise()._future_conductor = channel_conductor{&_waiters};
```

### CUTEX Conductor (Proposed)
```cpp
struct cutex_conductor : conductor_traits<async<>> {
    cutex* _mutex;
    
    void forward(async<>&& ctx) override {
        _mutex->_waiters.push(std::move(ctx));  // Queue in waiters
    }
    
    void cancel() override {
        // Remove from waiter queue if needed
    }
};

// In croxy::capture():
ctx.promise()._future_conductor = cutex_conductor{_mutex};
```

---

## 3. State Machine

### Lock States
```
UNLOCKED
    ↓
[Thread calls croxy.capture()]
    ↓
await_ready(): return true
    ↓
await_suspend(): return false (don't suspend)
    ↓
LOCKED (current thread owns lock)
    ↓
[Thread calls croxy.sync()]
    ↓
croxy destructor or explicit release:
    - Mark UNLOCKED
    - Pop waiter from queue
    - Resume via runner::reattach()
    ↓
Next waiter acquired lock
    ↓
LOCKED (different thread now owns)


OR if already locked:
    ↓
[Thread calls croxy.capture()]
    ↓
await_ready(): return false (not ready)
    ↓
await_suspend(): 
    - Setup conductor
    - return true (suspend)
    ↓
WAITING (queued in _waiters)
    ↓
[Lock holder releases]
    ↓
pop() waiter from queue
call runner::reattach()
    ↓
Waiter resumed on next yank()
```

---

## 4. Implementation Strategy

### 4.1 Core Components

```cpp
class cutex : public future_traits<cutex> {
private:
    // Lock state
    std::atomic<bool> _locked{false};
    
    // Queue of waiting contexts (MPSC to match channel pattern)
    typename nukes::dynamic::mpsc_queue<async<>> _waiters;
    
    struct cutex_conductor;
    friend cutex_conductor;
    
public:
    DECLARE_FUTURE(cutex)
    IMPORT_FUTURE_ENV
    
    // Awaitable interface
    bool await_ready() override {
        // Try to acquire lock atomically
        bool expected = false;
        return _locked.compare_exchange_strong(expected, true);
    }
    
    bool await_suspend(auto coroutine) {
        // Setup conductor for lock waiting
        coroutine.promise()._future_conductor = 
            cutex_conductor{this};
        return true;
    }
    
    void await_resume() {}
};
```

### 4.2 Proxy/Guard Type

```cpp
class croxy {
    cutex* _mutex;
    bool _acquired{false};
    
public:
    explicit croxy(cutex& mutex) : _mutex(&mutex) {}
    
    // Acquire lock
    async<> capture() {
        co_await *_mutex;
        _acquired = true;
    }
    
    // Release lock and wake next waiter
    void sync() {
        if (!_acquired) return;
        
        _mutex->_locked.store(false, std::memory_order_release);
        
        // Pop next waiter
        if (async<> waiter; _mutex->_waiters.pop(waiter)) {
            runner::reattach(std::move(waiter));
        }
        
        _acquired = false;
    }
    
    ~croxy() {
        sync();  // RAII semantics
    }
};
```

### 4.3 Conductor Implementation

```cpp
struct cutex::cutex_conductor : conductor_traits<async<>> {
    cutex* _mutex;
    
    explicit cutex_conductor(cutex* mutex) : _mutex(mutex) {}
    
    void forward(async<>&& ctx) override {
        // Store waiter in queue
        _mutex->_waiters.push(std::move(ctx));
    }
    
    void cancel() override {
        // TODO: Remove from queue if context cancelled
        // This is more complex - may need to find and remove node
    }
    
    ~cutex_conductor() override = default;
};
```

---

## 5. Comparison with Channel Implementation

### Similarities
1. Both use conductor pattern for forwarding contexts
2. Both use MPSC queue for storing waiting contexts
3. Both check lock state (data available vs lock free) in await_ready()
4. Both set conductor in await_suspend() if need to wait
5. Both use runner::reattach() to resume waiter

### Differences
| Aspect | Channel | CUTEX |
|--------|---------|-------|
| **Data** | Transmits data | Transmits ownership |
| **Trigger** | push() adds data | sync() releases lock |
| **Queue Type** | MPSC (multiple pushers) | MPSC (multiple waiters) |
| **Guard Type** | None needed | croxy for RAII |
| **Wake** | One per push | One per release |

---

## 6. Integration Points

### With Runner
```cpp
// In runner::yank():
if (is_conducted) {  // Has conductor (e.g., cutex_conductor)
    async_n->_data._coroutine.promise()._future_conductor->forward(
        std::forward<async<>>(async_n->_data)
    );
    _pool.release_node(async_n);  // Release from ready queue
}
// Conductor's forward() pushed to _waiters (different queue)
// Next release() calls runner::reattach() to move back
```

### With Promise
```cpp
// Promise stores conductor reference
template<typename promiseT>
bool await_suspend(std::coroutine_handle<promiseT> outer) {
    outer.promise()._future_conductor = cutex_conductor{this};
    return true;  // Suspend
}

// Runner checks on next yank()
if (promise._future_conductor) {
    promise._future_conductor->forward(context);
}
```

---

## 7. Test Pattern Mapping

### From tests/units.h
```cpp
inline ace::async<> racer(const int& max, std::string& shared_counter, ace::cutex& cut) {
    ace::croxy crx(cut);  // Guard creation
    for (volatile int i = 0; i < max; i = i + 1) {
        co_await crx.capture();       // CRITICAL: acquire lock
        shared_counter = std::to_string(std::stoi(shared_counter) + 1);
        crx.sync();                   // CRITICAL: release lock
    }
    co_await crx.capture();           // Final acquire
    std::cout << "'racer' finished\n";
}
```

### Expected Behavior
1. **crx.capture()** (first iteration):
   - await_ready() returns true (lock free)
   - Acquires lock atomically
   - Does not suspend
   
2. **crx.sync()**:
   - Marks lock as free
   - Pops one waiter from queue
   - Calls runner::reattach() to resume
   
3. **Other racers calling capture() while locked**:
   - await_ready() returns false
   - await_suspend() sets conductor and returns true
   - Context suspended, forwarded to _waiters queue
   - Later resumed when this racer calls sync()

4. **Final crx.capture()**:
   - May suspend if other racers still waiting
   - Guard destructor ensures cleanup

---

## 8. Memory Alignment and Padding

### From runner pattern
```cpp
struct alignas(ACE_CACHE_LINE_SIZE) cutex {
    // First member must be critical field accessed by multiple threads
    std::atomic<bool> _locked{false};
    ACE_CACHE_LINE(0)  // Padding to cache line boundary
    
    // Separate cache line for _waiters (less frequent access)
    alignas(ACE_CACHE_LINE_SIZE) 
    nukes::dynamic::mpsc_queue<async<>> _waiters;
};
```

### Rationale
- MPSC queue head/tail frequently updated
- Lock state checked very frequently
- Separate cache lines prevent false sharing
- Channel follows this pattern exactly

---

## 9. Thread Safety Analysis

### Atomic Operations
```cpp
// Non-atomic, thread-local in single runner:
bool ready = _locked.load(std::memory_order_acquire);

// Atomic for cross-thread safety:
bool exchanged = _locked.compare_exchange_strong(expected, true);

// Release semantics for synchronization:
_locked.store(false, std::memory_order_release);
```

### Why This Works
1. **Lock state**: Atomic with acquire/release semantics
2. **Waiter queue**: MPSC queue is already thread-safe
3. **Context passing**: MPSC queue handles concurrent pushes
4. **No lock contention**: Lock-free operations in hot path
5. **Fair ordering**: FIFO queue ensures no starvation

---

## 10. Comparison with Existing Implementations

### Channel (Most Similar)
```
Common patterns:
- conductor_carry storage in promise
- MPSC waiter queue
- forward() called by runner
- await_ready() checks condition
- await_suspend() sets conductor if needed

Differences:
- Channel: data + waiter queues
- CUTEX: ownership + waiter queue only
```

### Timeout (Clock Integration)
```
Common:
- conductor for delayed resumption
- Forwarding via conductor

Differences:
- Timeout: registered with clock service
- CUTEX: manually triggered by release
- Timeout: conductor cancellable
- CUTEX: conductor simpler
```

### Spawn (Command Pattern)
```
Common:
- Returns handle for observation
- Can be cancelled

Differences:
- Spawn: creates new task
- CUTEX: manages existing task state
- Spawn: command pattern
- CUTEX: future pattern
```

---

## 11. Error Handling

### Double-Acquire Prevention
```cpp
class croxy {
    // Guard against calling capture() twice without release
    bool _acquired{false};
    
    async<> capture() {
        if (_acquired) {
            // Log error or assert
            co_return;
        }
        co_await *_mutex;
        _acquired = true;
    }
};
```

### Cancellation Support
```cpp
void cutex_conductor::cancel() override {
    // Future cancellation means task was cancelled before lock acquired
    // If in _waiters queue, need to find and remove
    // Complex: may need reference to node pointer
    // See channel_conductor for similar challenge (marked TODO)
}
```

### Exception Safety
```cpp
class croxy {
    // RAII ensures release on exception
    ~croxy() {
        sync();  // Always releases, even if exception thrown
    }
};
```

---

## 12. Implementation Checklist

- [ ] Define `cutex` class inheriting `future_traits<cutex>`
- [ ] Implement `await_ready()` with atomic lock check
- [ ] Implement `await_suspend()` with conductor setup
- [ ] Implement `await_resume()` (empty)
- [ ] Define `croxy` guard class
- [ ] Implement `croxy::capture()` coroutine
- [ ] Implement `croxy::sync()` lock release
- [ ] Implement `croxy::~croxy()` RAII cleanup
- [ ] Define `cutex_conductor` struct
- [ ] Implement `conductor::forward()` to push waiter
- [ ] Implement `conductor::cancel()` for cancellation
- [ ] Add DECLARE_FUTURE macro usage
- [ ] Add thread-safe queue from nukes
- [ ] Align critical structures to cache lines
- [ ] Test single-threaded lock/unlock
- [ ] Test multi-threaded racing
- [ ] Test cancellation handling
- [ ] Verify no memory leaks
- [ ] Profile lock contention
- [ ] Validate fairness of waiter ordering

---

## 13. Code Template

```cpp
// include/ace/futures/cutex.h

#ifndef ACE_FUTURE_CUTEX_H
#define ACE_FUTURE_CUTEX_H

#include "future.h"
#include "ace/core/runner.h"
#include "ace/coroutines/context.h"
#include <nukes/dynamic/mpsc_queue.h>
#include <atomic>

namespace ace::futures {

class cutex : public future_traits<cutex> {
    
    struct cutex_conductor;
    friend cutex_conductor;
    
    alignas(ACE_CACHE_LINE_SIZE) std::atomic<bool> _locked{false};
    ACE_CACHE_LINE(0)
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

struct cutex::cutex_conductor : conductor_traits<async<>> {
    cutex* _mutex;
    
    explicit cutex_conductor(cutex* m) : _mutex(m) {}
    
    void forward(async<>&& ctx) override;
    void cancel() override;
    ~cutex_conductor() override = default;
};

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

namespace ace {
    using cutex = futures::cutex;
    using croxy = futures::croxy;
}

#endif // ACE_FUTURE_CUTEX_H
```

