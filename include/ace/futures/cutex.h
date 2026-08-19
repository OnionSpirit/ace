/**
 * @file cutex.h
 * @brief Cooperative Userspace MuTEX (@c ace::cutex) and its RAII proxy.
 *
 * @details The @b cutex is a cooperative, non-blocking mutual exclusion
 * primitive designed for use inside ACE coroutines.
 *
 * ### Fast path (uncontended)
 * @c try_lock() performs a single @c fetch_add(1) on @c _users.  If the result
 * is 0, the lock is acquired immediately — no suspension, no kernel call.
 *
 * ### Slow path (contended)
 * If @c try_lock() fails, @c await_suspend() installs a @c cutex_router into
 * the caller's promise.  The runner forwards the task into @c _waiters.  When
 * the current owner calls @c release(), it does a @c fetch_sub(1) and calls
 * @c notify() which pops the next waiter and reattaches it to its runner.
 *
 * ### Deadlock recovery
 * A rare race (OS interrupts the waiter between failed @c try_lock() and the
 * enqueue) can leave the mutex unlocked with a waiter stuck in the queue.
 * @c pending_notify() detects this and retries until the notification succeeds.
 *
 * ### Usage
 * @code{.cpp}
 * ace::cutex mtx;
 *
 * ace::task task() {
 *     volatile auto guard = ace::guard(mtx);
 *     auto future = co_await guard->capture();
 *     // --- critical section ---
 *     co_await future;
 *     guard->release();   // unlock (also called by ~proxy)
 *     co_return;
 * }
 * @endcode
 *
 * @warning Always declare the @c ace::guard (i.e., @c cutex::volatile_proxy)
 * as <b>@c volatile</b> to prevent the compiler from eliding its destructor.
 *
 * @see ace::guard, ace::futures::capture_future, ace::futures::cutex::proxy
 */
#ifndef ACE_FUTURE_CUTEX_H
#define ACE_FUTURE_CUTEX_H

#include <nukes/dynamic/roaming_mpsc_queue.h>

#include <ace/core/traits/future.h>
#include <ace/core/runner.h>
#include <ace/core/async.h>

#include <ace/futures/reattach.h>
#include <ace/futures/roaming.h>

#include "ace/console.h"


namespace ace::futures {

    /**
     * @brief Core cutex state — user counter and the waiter queue.
     */
    struct cutex_control {

        // NOTE: <int> instead of <uint64_t> because unsigned type may ruin process on overflow after subtract
        std::atomic<long>                           _users            {0};         ///< Number of active users (0 = unlocked).
        nukes::dynamic::roaming_mpsc_queue<task>    _waiters          { };            ///< Tasks waiting to acquire the mutex.

        /**
         * @brief Attempt to acquire the mutex without suspending.
         * @details Atomically increments @c _users.  If the pre-increment value
         * was 0, the lock is acquired.
         * @return @c true if the lock was acquired.
         */
        bool try_lock() noexcept;

        /**
         * @brief Attempt to wake one waiter from @c _waiters.
         * @details Pops one context from the waiters queue and calls
         * @c runner::reattach().  When the waiter allows roaming, its runner
         * is re-pointed to the current thread before waking (rescheduling);
         * otherwise a thread-safe cross-runner reattach is used.
         * @return @c true if a waiter was successfully notified.
         */
        bool notify();

        /**
         * @brief Deadlock resolution coroutine.
         *
         * @details The cutex can enter a deadlock state when an OS preemption
         * interrupts a waiter between a failed @c try_lock() and its enqueue
         * into @c _waiters.  The sequence is:
         *
         *  1. Thread @b A owns the cutex.
         *  2. Thread @b B calls @c try_lock() — fails (returns @c false).
         *  3. OS interrupts thread @b B before it enqueues into @c _waiters.
         *  4. Thread @b A calls @c release() → @c notify() — queue is empty → no one woken.
         *  5. Thread @b B resumes and enqueues itself → permanently stuck.
         *
         * @c pending_notify() detects this by retrying @c notify() while
         * @c _users > 0 (meaning at least one waiter exists), suspending between
         * retries.
         *
         * @return An @c ace::task coroutine that retries notification.
         */
        task pending_notify() noexcept;
    };


    /**
     * @brief Internal implementation of the cutex locking future.
     * @c capture_future is the awaitable returned by @c cutex::capture().
     * It is separated from @c cutex itself to enforce RAII discipline through
     * the @c proxy wrapper.
     *
     * @b resumeType - @c void
     *
     * @see ace::futures::cutex
     */
    class ACE_AWAIT_NODISCARD capture_future : public core::traits::future_traits<capture_future> {

        struct cutex_router;
        friend cutex_router;

        cutex_control* _control { nullptr }; ///< Underlying cutex state.

    public:

        omni_runner _runner {};      ///< Runner captured at suspension time.
        bool _roaming { false };     ///< Whether the waiter may migrate while waiting.
        bool _roaming_state {};      ///< Original roaming value of the caller, restored on resume.

        IMPORT_FUTURE_ENV(capture_future)

        /// @brief Default construction is forbidden — a control block is required.
        capture_future() = delete;

        /**
         * @brief Binds the future to a cutex control block.
         * @param control_  Cutex state to lock.
         * @param roaming   Whether migration is allowed while waiting.
         */
        explicit capture_future(cutex_control* control_, const bool roaming = false) noexcept
            : _control(control_)
            , _roaming(roaming) {}

        /// @brief C++20 awaitable protocol — attempt fast-path acquire.
        bool await_ready() override { return _control->try_lock(); }

        /**
         * @brief C++20 awaitable protocol — suspend and enqueue for wakeup.
         * @details Installs a @c cutex_router so the runner forwards the
         * calling context into @c _waiters.
         * @param coroutine  Handle to the suspending coroutine's promise.
         * @return Always @c true (always suspends on the slow path).
         */
        bool await_suspend(auto coroutine);

        void await_resume() {} ///< No value produced; mutex is already acquired when resumed.

        /// @brief Default destructor.
        ~capture_future() override = default;

    };

    /**
     * @brief Cooperative Userspace MuTEX — public API wrapper.
     *
     * @details @c cutex is the user-facing type.  It inherits from
     * @c capture_future (protected) and exposes only the @c volatile_proxy RAII
     * interface to prevent accidental direct @c co_await-ing.
     *
     * @par Usage
     * @code{.cpp}
     * ace::cutex mtx;
     * ace::task task() {
     *     volatile auto g = ace::guard(mtx);
     *     auto f = co_await g->capture();
     *     // critical section
     *     co_await f;
     *     g->release();
     *     co_return;
     * }
     * @endcode
     *
     * @see ace::guard (alias for @c cutex::volatile_proxy)
     */
    class cutex {

        /**
         * @brief Creates the lock future.
         * @param roaming Whether the waiting task may migrate runners.
         * @return The @c capture_future to @c co_await.
         */
        [[nodiscard]] auto capture(bool roaming) noexcept -> capture_future;

        /**
         * @brief Unlocks the cutex and wakes the next waiter.
         */
        void release() noexcept;

        cutex_control _control { }; ///< Core mutex state.

    public:

        /// @brief Default constructor — unlocked mutex.
        cutex() = default;

        class proxy;

        cutex(const cutex&) = delete; ///< Mutexes are not copyable.
        cutex(cutex&&) = delete;      ///< Mutexes are not movable.

        /// @brief Default destructor.
        ~cutex() = default;
    };

    /**
     * @brief RAII proxy that enforces balanced @c capture() / @c release() calls.
     *
     * @details The proxy prevents calling @c capture() twice without an
     * intervening @c release(), and automatically calls @c release() on destruction.
     *
     * Declare as @c volatile to prevent the compiler from eliding the destructor:
     * @code{.cpp}
     * volatile auto guard = ace::guard(mtx);
     * @endcode
     *
     * @warning Sharing a proxy between coroutines is undefined behaviour.
     */
    class cutex::proxy {

        cutex& _cutex;                    ///< Managed mutex.
        omni_runner _runner {};           ///< Original runner (for @c sync() restore).
        bool _is_released { true };    ///< @c Equals true when the mutex is not held.
        bool _is_manual { false };     ///< @c Equals true if requires manual @c release(). @c cutex captured by @c sync()
        bool _roaming_state { true };  ///< Task @c roaming value before interacting with @c cutex

    public:

        /// @brief Default construction is forbidden — a cutex is required.
        proxy() = delete;
        /// @brief Copying a proxy is forbidden.
        proxy(const proxy&) = delete;
        /// @brief Moving a proxy is forbidden.
        proxy(proxy&&) = delete;

        /**
         * @brief Construct a proxy bound to the given cutex.
         * @param cx  The cutex to manage.  Must outlive this proxy.
         */
        explicit proxy(cutex& cx) : _cutex(cx) { }

        /**
         * @brief Acquire the cutex.
         * @details Returns the underlying @c capture_future& so the caller can
         * @c co_await it.  Throws @c std::logic_error if called twice without
         * an intervening @c release().
         * @return Reference to the cutex's @c capture_future interface.
         * @throws std::logic_error if called while the lock is already held.
         */
        ACE_AWAIT_NODISCARD auto capture() -> capture_future {
            if (not _is_released)
                throw std::logic_error {"duplicated 'capture()/sync()' operation before 'release()'"};
            _is_released = false;
            _is_manual = false;
            return _cutex.capture(false);
        };

        /**
         * @brief Acquire the cutex.
         * @details Under race condition reschedules calling context to the thread that owns @c cutex.
         * And reschedules it back on @c release()
         * @return Reference to the cutex's @c capture_future interface.
         * @warning Using thread local data is forbidden under this type of lock.
         * Because @c sync() may reschedule calling task to another thread.
         * Also destructor of the guard will not reattach @c task back to the source thread.
         * It is recommended to call @c release() manually
         * @throws std::logic_error if called while the lock is already held.
         */
        ACE_AWAIT_NODISCARD auto sync() -> capture_future {
            if (not _is_released)
                throw std::logic_error {"duplicated 'capture()/sync()' operation before 'release()'"};
            _is_released = false;
            _is_manual = true;
            // NOTE: Creating capture future
            auto capt = _cutex.capture(true);
            // NOTE: Storing original runner and roaming value
            {
                _runner = capt._runner;
                _roaming_state = capt._roaming_state;
            }
            return capt;
        };

        /**
         * @brief Release the cutex.
         * @details No-op if the lock is not currently held.
         */
        ACE_AWAIT_NODISCARD promise<> release() noexcept {
            if (not _is_released) {
                _cutex.release();
                _is_released = true;
                _is_manual = false;
            }
            // NOTE: Reattaching task to the original runner
            co_await ace::reattach{_runner};
            // NOTE: Resetting roaming value to original
            co_await roaming(_roaming_state);
            co_return;
        };

        /**
         * @brief Destructor. Automatically calls @c release() if not already released.
         * @warning Throws logical exception if @c cutex was captured by @c sync() and not released manually
         */
        ~proxy() {
            if (not _is_released) {
                _cutex.release();
                if (_is_manual)
                    throw std::logic_error {"manual 'release()' required on 'sync()' type of lock"};
            }
        }
    };

} // end namespace ace::futures

// NOTE: The short aliases ace::cutex / ace::guard / ace::cutex_control /
// ace::capture_future are only exposed when ace/ace.h (quick-start header) was
// included before this file — its ACE_H guard switches aliases on. The guard
// also makes ace::reattach visible (from futures/reattach.h), which is
// referenced by cutex::proxy::release().
#ifdef ACE_H
namespace ace {
    /// @brief Short alias for the cutex type.
    using cutex = futures::cutex;
    /// @brief RAII guard for a cutex — an alias for @c cutex::proxy.
    using guard = cutex::proxy;
    /// @brief Core cutex state (user counter and the waiter queue).
    using cutex_control = futures::cutex_control;
    /// @brief Awaitable returned by @c cutex::proxy::capture() / sync().
    using capture_future = futures::capture_future;
}
#endif

//==============================- DEFINITIONS -==================================

#define ACE_FUTURE_CAPTURE_FUTURE_SPACE \
ace::futures::capture_future::

#define ACE_FUTURE_CAPTURE_FUTURE_MEMBER(returnT) \
returnT ACE_FUTURE_CAPTURE_FUTURE_SPACE

#define ACE_FUTURE_CUTEX_CORE_SPACE \
ace::futures::cutex_control::

#define ACE_FUTURE_CUTEX_CONTROL_MEMBER(returnT) \
returnT ACE_FUTURE_CUTEX_CORE_SPACE


/**
 * @brief Router that enqueues waiting tasks into the cutex waiter queue.
 *
 * @details On @c redirect() the suspended task is pushed into the cutex's
 * waiter storage; cancellation is a no-op (waiters are woken by
 * @c cutex_control::notify() on release).
 */
struct ACE_FUTURE_CAPTURE_FUTURE_SPACE cutex_router : runner_router {

    /// @brief Default construction is forbidden — a capture future is required.
    cutex_router() = delete;

    /**
     * @brief Binds the router to the owning capture future.
     * @param cutex_ Pointer to the capture future.
     */
    explicit cutex_router(capture_future* cutex_)
        : _cutex(cutex_) {};

    /**
     * @brief Enqueues the waiting task into the cutex's waiter queue.
     * @param node Task node of the suspended waiter.
     */
    void redirect(omni_node node) override {
        _cutex->_control->_waiters.push_node(node);
    }

    // NOTE: Tasks is resuming with wiped router.
    // NOTE: Placing into waiters queue is moving operation and also wont affect async handler inner state.
    // NOTE: So we can cancel it by task handler
    // NOTE: If task has handlers it means that task is thread local with canceling task.
    // NOTE: No extra release needed.
    // NOTE: Cutex can be interacted only via it's RAII proxy, so extra manual 'release()' not needed.
    // NOTE: Maybe... Sometimes... I will add ejecting from mpsc queue by node handle.
    // NOTE: But Im not sure that mpsc or mpmc would stay consistent
    /**
     * @brief Cancellation is a no-op — waiters are released via @c cutex_control::notify().
     */
    void cancel() override {  }

    ~cutex_router() override = default;

    capture_future* _cutex; ///< Owning capture future.
};

ACE_FUTURE_CUTEX_CONTROL_MEMBER(bool)
/**
 * @brief Fast-path lock acquire.
 * @return @c true when the lock was acquired (pre-increment value was 0).
 */
try_lock() noexcept { return _users.fetch_add(1, std::memory_order_acq_rel) == 0; }

ACE_FUTURE_CUTEX_CONTROL_MEMBER(bool)
/**
 * @brief Wakes one waiter, migrating it if allowed.
 * @return @c true when a waiter was notified, @c false when the queue was empty.
 */
notify() {
    typedef nukes::dynamic::roaming_mpsc_queue<task>::node_t waiter_node_t;
    waiter_node_t* waiter_node = _waiters.pop_node();

    // NOTE: Trying to fetch next waiter and reattach it to the runner.
    // NOTE: If it has failed to fetch then pending notification needed
    if (not waiter_node)
        return false;

    // NOTE: Reattaching fast if pool is the same
    if (core::runner::get() == waiter_node->_data._coroutine.promise()._runner)
        core::runner::reattach_front(waiter_node);

    // NOTE: Rescheduling waiter if rescheduling allowed
    else if (waiter_node->_data._coroutine.promise()._roaming) {
        waiter_node->_data._coroutine.promise()._runner = core::runner::get();
        core::runner::reattach_front(waiter_node);
    }
    // NOTE: Threadsafe reattach at worst case
    else
        core::runner::reattach(waiter_node);

    return true;
}

ACE_FUTURE_CUTEX_CONTROL_MEMBER(ace::task)
/**
 * @brief Deadlock recovery — retries notification while users remain.
 */
pending_notify() noexcept {
    do {
        if (notify()) co_return;
        // NOTE: If notify still has no success, suspend and retry
        co_await suspend();
    } while (_users.load(std::memory_order_acquire) > 0);
}

ACE_FUTURE_CAPTURE_FUTURE_MEMBER(bool)
/**
 * @brief Installs the cutex router and stores the caller's context.
 * @param coroutine Caller coroutine promise accessor.
 * @return Always @c true — the caller always suspends on the slow path.
 */
await_suspend(auto coroutine) {
    // NOTE: Setting router for dispatch to the cutex waiters queue
    coroutine.promise()._runner_router = cutex_router{this};
    _runner = coroutine.promise()._runner;
    _roaming_state = coroutine.promise()._roaming;
    coroutine.promise()._roaming = _roaming;
    return true;
}

#undef ACE_FUTURE_CAPTURE_FUTURE_MEMBER
#undef ACE_FUTURE_CAPTURE_FUTURE_SPACE

#define ACE_FUTURE_CUTEX_SPACE \
ace::futures::cutex::

#define ACE_FUTURE_CUTEX_MEMBER(returnT) \
returnT ACE_FUTURE_CUTEX_SPACE


ACE_FUTURE_CUTEX_MEMBER(ace::futures::capture_future)
/**
 * @brief Creates the lock future for this cutex.
 * @param roaming Whether the waiting task may migrate runners.
 * @return The @c capture_future to @c co_await.
 */
capture(const bool roaming) noexcept { return capture_future{&_control, roaming}; }

ACE_FUTURE_CUTEX_MEMBER(void)
/**
 * @brief Unlocks the cutex and wakes the next waiter.
 */
release() noexcept {
    // NOTE: Subtract users because leaving cutex
    // NOTE: If there are some waiters but fetching is failed
    // NOTE: then scheduling delayed notification
    if (_control._users.fetch_sub(1, std::memory_order_acq_rel) > 1 and not _control.notify())
        schedule(_control.pending_notify());
}

#undef ACE_FUTURE_CUTEX_MEMBER
#undef ACE_FUTURE_CUTEX_SPACE
#endif //ACE_FUTURE_CUTEX_H
