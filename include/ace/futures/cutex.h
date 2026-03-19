/**
 * @file
 * @details This file contains cutex (coroutine mutex) and croxy (RAII guard) declarations.
 * cutex is a userspace mutex built on C++20 coroutines that works without syscalls.
 * Suspended waiters are stored in an MPSC queue and reattached to the runner on lock release.
 */
#ifndef ACE_FUTURE_CUTEX_H
#define ACE_FUTURE_CUTEX_H

#include "future.h"
#include "ace/core/runner.h"
#include "ace/coroutines/context.h"
#include "ace/common/terms.h"
#include <nukes/dynamic/mpsc_queue.h>
#include <atomic>
#include <coroutine>

namespace ace::futures {

class croxy;

/**
 * @details Userspace coroutine mutex. Implements future_traits to be awaitable.
 * Lock acquisition is atomic (compare_exchange). Contested waiters suspend via
 * the cutex_conductor and are stored in an MPSC queue until the lock is released.
 *
 * Rescheduling mode (set_rescheduling(true)):
 *   croxy::capture() yields via std::suspend_always after acquiring the lock.
 *   This forces a round-trip through the runner pool, giving other coroutines on
 *   other runners a scheduling opportunity between release and re-acquisition.
 */
class cutex : public future_traits<cutex> {

    struct cutex_conductor;
    friend cutex_conductor;
    friend croxy;

    /// Lock state - on its own cache line to avoid false sharing with _waiters
    alignas(ACE_CACHE_LINE_SIZE) std::atomic<bool> _locked{false};
    bool _rescheduling{false};
    ACE_CACHE_LINE(0)

    /// Awaiter queue - on its own cache line
    alignas(ACE_CACHE_LINE_SIZE)
    nukes::dynamic::mpsc_queue<async<>> _waiters;

public:

    DECLARE_FUTURE(cutex)
    IMPORT_FUTURE_ENV

    cutex() = default;
    ~cutex() override = default;

    /**
     * @details Atomically tries to acquire the lock via compare_exchange.
     * @return true if lock was free and is now owned by caller, false if contested.
     */
    bool await_ready() override;

    /**
     * @details Sets up the conductor so the runner queues this context in _waiters.
     * Always returns true (suspends caller).
     */
    bool await_suspend(auto coroutine);

    /// No-op: lock is already held when await_resume() is called
    void await_resume() {}

    /**
     * @details Enables or disables rescheduling mode.
     * When enabled, croxy::capture() yields to the runner pool after acquiring the
     * lock, improving fairness under high contention across multiple runners.
     */
    void set_rescheduling(bool value) { _rescheduling = value; }
};


/**
 * @details Conductor that stores suspended contexts in the cutex's awaiter queue.
 * The ONLY place runner::reattach() is called is in croxy::sync(), NOT here.
 */
struct cutex::cutex_conductor : conductor_handler_t {

    cutex_conductor() = delete;

    explicit cutex_conductor(cutex* m) : _mutex(m) {}

    /// Queue the suspended context - do NOT call reattach here
    void forward(async<>&& ctx) override {
        _mutex->_waiters.push(std::move(ctx));
    }

    // TODO: implement proper cancellation (find and remove node from queue)
    void cancel() override {}

    ~cutex_conductor() override = default;

    cutex* _mutex;
};


inline bool cutex::await_ready() {
    bool expected = false;
    return _locked.compare_exchange_strong(expected, true);
}

inline bool cutex::await_suspend(auto coroutine) {
    coroutine.promise()._future_conductor = cutex_conductor{this};
    return true;
}


/**
 * @details RAII guard for cutex. Acquires lock via capture(), releases via sync().
 * Destructor calls sync() to ensure the lock is always released.
 */
class croxy {

    cutex* _mutex;
    bool _acquired{false};

public:

    explicit croxy(cutex& mutex) : _mutex(&mutex) {}

    ~croxy() { sync(); }

    croxy(const croxy&) = delete;
    croxy& operator=(const croxy&) = delete;

    /**
     * @details Coroutine that acquires the lock. May suspend if lock is contested.
     * When rescheduling is enabled, yields via suspend_always after acquiring the
     * lock so that other coroutines get a scheduling turn before the critical section.
     * Use as: co_await guard.capture();
     */
    async<> capture() {
        co_await *_mutex;
        _acquired = true;
        if (_mutex->_rescheduling) {
            // Force a round-trip through the runner pool.
            // suspend_always sets _future=nullptr in the promise so the next
            // runner cycle resumes this context immediately (no future to check).
            co_await std::suspend_always{};
        }
    }

    /**
     * @details Releases the lock and wakes the next waiter, if any.
     * Idempotent: safe to call multiple times or from destructor.
     *
     * Pop order: _waiters.pop() happens while the lock is still held so that
     * only one thread (the current lock owner) ever calls pop() at a time —
     * the MPSC queue guarantees safety for multiple producers but only a
     * single consumer.  Releasing the lock first would let another racer
     * acquire it and call pop() concurrently, violating that invariant.
     *
     * Conductor reset: the waiter's _future_conductor still points to a
     * cutex_conductor that lives in the capture coroutine's _conductor_area.
     * If left set, runner::yank() sees is_conducted=true and forwards the
     * waiter back to _waiters while capture already holds the lock — deadlock.
     * reset() nulls the pointer without touching the object (we don't own it;
     * the capture frame will clean it up when the coroutine is destroyed).
     */
    void sync() {
        if (!_acquired) return;
        _acquired = false;

        // Pop the next waiter while we still hold the lock (single-consumer safety)
        async<> waiter;
        const bool has_waiter = _mutex->_waiters.pop(waiter);

        // Release the lock
        _mutex->_locked.store(false, std::memory_order_release);

        if (has_waiter) {
            // Null the conductor pointer so runner keeps the waiter in the
            // runner pool instead of re-forwarding it to _waiters.
            waiter._coroutine.promise()._future_conductor.reset();
            core::runner::reattach(std::move(waiter));
        }
    }
};

} // namespace ace::futures


namespace ace {
    using cutex = futures::cutex;
    using croxy = futures::croxy;
}

#endif // ACE_FUTURE_CUTEX_H
