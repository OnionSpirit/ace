/**
 * @file cutex.h
 * @brief Cooperative Userspace MuTEX (`ace::cutex`) and its RAII proxy.
 *
 * @details The **cutex** is a cooperative, non-blocking mutual exclusion
 * primitive designed for use inside ACE coroutines.
 *
 * ### Fast path (uncontended)
 * `try_lock()` performs a single `fetch_add(1)` on `_users`.  If the result
 * is 0, the lock is acquired immediately — no suspension, no kernel call.
 *
 * ### Slow path (contended)
 * If `try_lock()` fails, `await_suspend()` installs a `cutex_conductor` into
 * the caller's promise.  The runner forwards the task into `_waiters`.  When
 * the current owner calls `sync()`, it does a `fetch_sub(1)` and calls
 * `notify()` which pops the next waiter and reattaches it to its runner.
 *
 * ### Deadlock recovery
 * A rare race (OS interrupts the waiter between failed `try_lock()` and the
 * enqueue) can leave the mutex unlocked with a waiter stuck in the queue.
 * `pending_notify()` detects this and retries until the notification succeeds.
 *
 * ### Usage
 * @code{.cpp}
 * ace::cutex mtx;
 *
 * ace::async<> task() {
 *     volatile auto guard = ace::guard(mtx);
 *     auto future = co_await guard->capture();
 *     // --- critical section ---
 *     co_await future;
 *     guard->sync();   // unlock (also called by ~proxy)
 *     co_return;
 * }
 * @endcode
 *
 * @warning Always declare the `ace::guard` (i.e., `cutex::volatile_proxy`)
 * as **`volatile`** to prevent the compiler from eliding its destructor.
 *
 * @see ace::guard, ace::futures::cutex_future, ace::futures::cutex::proxy
 */
#ifndef ACE_FUTURE_CUTEX_H
#define ACE_FUTURE_CUTEX_H
#include "future.h"
#include "ace/core/runner.h"
#include "ace/coroutines/context.h"
#include "nukes/dynamic/roaming_mpsc_queue.h"


namespace ace::futures {

    /**
     * @brief Internal implementation of the cutex locking future.
     *
     * @details `cutex_future` is the awaitable returned by `cutex::capture()`.
     * It is separated from `cutex` itself to enforce RAII discipline through
     * the `proxy` wrapper.
     *
     * @see ace::futures::cutex
     */
    class cutex_future : public future_traits<cutex_future> {

        struct cutex_conductor;
        friend cutex_conductor;

    public:

        IMPORT_FUTURE_ENV(cutex_future)

        // NOTE: <int> instead of <uint64_t> because unsigned type may ruin process on overflow after subtract
        std::atomic<int> _users { 0 };                                  ///< Number of active users (0 = unlocked).
        nukes::dynamic::roaming_mpsc_queue<async<>> _waiters {};        ///< Tasks waiting to acquire the mutex.
        std::atomic<runner_pool_t*> _runner_pool { nullptr };           ///< Runner pool of the task that currently owns the lock (used for rescheduling).
        bool _rescheduling { false };                                   ///< When `true`, released waiters are migrated to `_runner_pool`.

        /**
         * @brief Attempt to wake one waiter from `_waiters`.
         * @details Pops one context from the waiters queue and calls
         * `runner::reattach()`.  If `_rescheduling` is set and the waiter
         * does not support roaming, updates `_runner_pool` from the waiter's
         * runner.
         * @return `true` if a waiter was successfully notified.
         */
        bool notify() noexcept;

        /**
         * @brief Deadlock resolution coroutine.
         *
         * @details The cutex can enter a deadlock state when an OS preemption
         * interrupts a waiter between a failed `try_lock()` and its enqueue
         * into `_waiters`.  The sequence is:
         *
         *  1. Thread **A** owns the cutex.
         *  2. Thread **B** calls `try_lock()` — fails (returns `false`).
         *  3. OS interrupts thread **B** before it enqueues into `_waiters`.
         *  4. Thread **A** calls `sync()` → `notify()` — queue is empty → no one woken.
         *  5. Thread **B** resumes and enqueues itself → permanently stuck.
         *
         * `pending_notify()` detects this by retrying `notify()` while
         * `_users > 0` (meaning at least one waiter exists), suspending between
         * retries.
         *
         * @return An `ace::async<>` coroutine that retries notification.
         */
        async<> pending_notify() noexcept;

        /**
         * @brief Attempt to acquire the mutex without suspending.
         * @details Atomically increments `_users`.  If the pre-increment value
         * was 0, the lock is acquired.
         * @return `true` if the lock was acquired.
         */
        bool try_lock() noexcept;

        /// @brief C++20 awaitable protocol — attempt fast-path acquire.
        bool await_ready() override { return try_lock(); }

        /**
         * @brief C++20 awaitable protocol — suspend and enqueue for wakeup.
         * @details Installs a `cutex_conductor` so the runner forwards the
         * calling context into `_waiters`.
         * @param coroutine  Handle to the suspending coroutine's promise.
         * @return Always `true` (always suspends on the slow path).
         */
        bool await_suspend(auto coroutine);

        void await_resume() {} ///< No value produced; mutex is already acquired when resumed.

        ~cutex_future() override = default;

        /**
         * @brief Enable or disable rescheduling mode.
         * @details When `true`, released waiters are migrated to the runner
         * pool of the task that most recently released the cutex.  This keeps
         * the critical section on the same CPU for better cache locality.
         * @param rs  `true` to enable rescheduling.
         */
        void set_rescheduling(const bool rs) noexcept { _rescheduling = rs; }

        /// @brief Query the rescheduling mode.
        [[nodiscard]] bool get_rescheduling() const noexcept { return _rescheduling; }
    };

    /**
     * @brief Cooperative Userspace MuTEX — public API wrapper.
     *
     * @details `cutex` is the user-facing type.  It inherits from
     * `cutex_future` (protected) and exposes only the `volatile_proxy` RAII
     * interface to prevent accidental direct `co_await`-ing.
     *
     * @par Usage
     * @code{.cpp}
     * ace::cutex mtx;
     * ace::async<> task() {
     *     volatile auto g = ace::guard(mtx);
     *     auto f = co_await g->capture();
     *     // critical section
     *     co_await f;
     *     g->sync();
     *     co_return;
     * }
     * @endcode
     *
     * @see ace::guard (alias for `cutex::volatile_proxy`)
     */
    class cutex final : protected cutex_future {

        [[nodiscard]] auto capture() noexcept -> cutex_future&;

        void sync() noexcept;

        class proxy;

    public:

        cutex() = default;

        cutex(const cutex&) = delete; ///< Mutexes are not copyable.
        cutex(cutex&&) = delete;      ///< Mutexes are not movable.

        /// @brief RAII proxy type alias.  Use `volatile` to prevent elision.
        typedef volatile proxy volatile_proxy;

        ~cutex() override = default;

        using cutex_future::set_rescheduling;
        using cutex_future::get_rescheduling;
    };

    /**
     * @brief RAII proxy that enforces balanced `capture()` / `sync()` calls.
     *
     * @details The proxy prevents calling `capture()` twice without an
     * intervening `sync()`, and automatically calls `sync()` on destruction.
     *
     * Declare as `volatile` to prevent the compiler from eliding the destructor:
     * @code{.cpp}
     * volatile auto guard = ace::guard(mtx);
     * @endcode
     *
     * @warning Sharing a proxy between coroutines is undefined behaviour.
     */
    class cutex::proxy {

        cutex& _cutex;
        bool _is_synced { true }; ///< `true` when the mutex is not held.

    public:

        proxy() = delete;
        proxy(const proxy&) = delete;
        proxy(proxy&&) = delete;

        /**
         * @brief Construct a proxy bound to the given cutex.
         * @param cx  The cutex to manage.  Must outlive this proxy.
         */
        explicit proxy(cutex& cx) : _cutex(cx) { }

        /**
         * @brief Acquire the cutex.
         * @details Returns the underlying `cutex_future&` so the caller can
         * `co_await` it.  Throws `std::logic_error` if called twice without
         * an intervening `sync()`.
         * @return Reference to the cutex's `cutex_future` interface.
         * @throws std::logic_error if called while the lock is already held.
         */
        [[nodiscard]] auto capture() volatile -> cutex_future& {
            if (not _is_synced)
                throw std::logic_error {"cutex 'capture()' before 'sync()'"};
            _is_synced = false;
            return _cutex.capture();
        };

        /**
         * @brief Release the cutex.
         * @details No-op if the lock is not currently held.
         */
        void sync() volatile noexcept { if (not _is_synced) { _cutex.sync(); _is_synced = true; } };

        /// @brief Destructor.  Automatically calls `sync()` if not already synced.
        ~proxy() { sync(); }
    };

} // end namespace ace::futures

namespace ace {
    using futures::cutex;
    using guard = cutex::volatile_proxy;
}

//==============================- DEFINITIONS -==================================

#define ACE_FUTURE_CUTEX_FUTURE_SPACE \
ace::futures::cutex_future::

#define ACE_FUTURE_CUTEX_FUTURE_MEMBER(returnT) \
returnT ACE_FUTURE_CUTEX_FUTURE_SPACE


struct ACE_FUTURE_CUTEX_FUTURE_SPACE cutex_conductor : conductor_handler_t {

    cutex_conductor() = delete;

    explicit cutex_conductor(cutex_future* cutex_)
        : _cutex(cutex_) {};

    void forward(async<>&& ctx) override {
        while (not _cutex->_waiters.push(std::move(ctx)));
    }

    // NOTE: Tasks is resuming with wiped conductor.
    // NOTE: Placing into waiters queue is moving operation and also wont affect context handler inner state.
    // NOTE: So we can cancel it by task handler
    // NOTE: If task has handlers it means that task is thread local with canceling task.
    // NOTE: No extra sync needed.
    // NOTE: Cutex can be interacted only via it's RAII proxy, so extra manual 'sync()' not needed.
    // NOTE: Maybe... Sometimes... I will add ejecting from mpsc queue by node handle.
    // NOTE: But Im not sure that mpsc or mpmc would stay consistent
    void cancel() override {  }

    ~cutex_conductor() override = default;

    cutex_future* _cutex;
};

ACE_FUTURE_CUTEX_FUTURE_MEMBER(bool)
try_lock() noexcept {
    return _users.fetch_add(1, std::memory_order_acq_rel) == 0;
}

ACE_FUTURE_CUTEX_FUTURE_MEMBER(bool)
notify() noexcept {
    async<> waiter;
    // NOTE: Trying to fetch next waiter and release it on the runner
    if (not _waiters.pop(waiter))
        return false;
    // NOTE: Updating rescheduling pool if rescheduling mode is on and waiter forbids roaming
    if (const bool roaming = waiter._coroutine.promise()._roaming; _rescheduling and not roaming)
        _runner_pool.store(waiter._coroutine.promise()._runner_pool, std::memory_order_release);
    // NOTE: Rescheduling waiter if rescheduling mode is on and waiter supports roaming
    else if (_rescheduling)
        waiter._coroutine.promise()._runner_pool = _runner_pool.load(std::memory_order_acquire);
    core::runner::reattach(std::move(waiter));
    return true;
}

ACE_FUTURE_CUTEX_FUTURE_MEMBER(ace::async<>)
pending_notify() noexcept {
    do {
        if (notify()) co_return;
        // NOTE: If notify still has no success, suspend and retry
        co_await suspend();
    } while (_users.load(std::memory_order_acquire) > 0);
}

ACE_FUTURE_CUTEX_FUTURE_MEMBER(bool)
await_suspend(auto coroutine) {
    // NOTE: Selecting rescheduling pool if it doesn't set and rescheduling mode on
    if (not _runner_pool.load(std::memory_order_acquire))
        _runner_pool.store(coroutine.promise()._runner_pool, std::memory_order_release);

    // NOTE: Setting conductor for dispatch to the cutex waiters queue
    coroutine.promise()._runner_conductor = cutex_conductor{this};
    return true;
}

#undef ACE_FUTURE_CUTEX_FUTURE_MEMBER
#undef ACE_FUTURE_CUTEX_FUTURE_SPACE

#define ACE_FUTURE_CUTEX_SPACE \
ace::futures::cutex::

#define ACE_FUTURE_CUTEX_MEMBER(returnT) \
returnT ACE_FUTURE_CUTEX_SPACE


ACE_FUTURE_CUTEX_MEMBER(ace::futures::cutex_future&)
capture() noexcept { return *static_cast<cutex_future*>(this); }

ACE_FUTURE_CUTEX_MEMBER(void)
sync() noexcept {
    // NOTE: Subtract users because leaving cutex
    // NOTE: If there are some waiters but fetching is failed
    // NOTE: then scheduling delayed notification
    if (_users.fetch_sub(1, std::memory_order_acq_rel) > 1 and not notify())
        schedule(pending_notify());
}

#undef ACE_FUTURE_CUTEX_MEMBER
#undef ACE_FUTURE_CUTEX_SPACE
#endif //ACE_FUTURE_CUTEX_H
