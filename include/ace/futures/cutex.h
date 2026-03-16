#ifndef ACE_FUTURE_CUTEX_H
#define ACE_FUTURE_CUTEX_H
#include "future.h"
#include "ace/core/runner.h"
#include "ace/coroutines/context.h"
#include "nukes/dynamic/mpmc_queue.h"


namespace ace::futures {

    // TODO: This is a temp solution. Need to fix nukes::dynamic::mpsc_queue to prevent data loss. Cutex is stable
    struct spinlock_waiters_std_queue {
        std::queue<async<>> _queue {};
        std::atomic_flag _lock = ATOMIC_FLAG_INIT;

        bool push(async<>&& waiter) {
            while (_lock.test_and_set(std::memory_order_acq_rel)) {};
            _queue.push(std::move(waiter));
            _lock.clear();
            return true;
        }

        bool pop(async<>& waiter) {
            while (_lock.test_and_set(std::memory_order_acq_rel)) {};
            if (_queue.empty()) {
                _lock.clear();
                return false;
            }
            waiter = std::forward<async<>>(_queue.front());
            _queue.pop();
            _lock.clear();
            return true;
        }
    };

    class cutex_future : public future_traits<cutex_future> {

        struct cutex_conductor;
        friend cutex_conductor;

    public:

        DECLARE_FUTURE(cutex_future)
        IMPORT_FUTURE_ENV

        // NOTE: <int> instead of <uint64_t> because unsigned type may ruin process on overflow after subtract
        std::atomic<int> _users { 0 };
        // TODO: This is a temp solution. Need to fix nukes::dynamic::mpsc_queue to prevent data loss.
        // nukes::dynamic::mpsc_queue<async<>> _waiters {};
        spinlock_waiters_std_queue _waiters {};
        std::atomic<runner_pool_t*> _runner_pool { nullptr };
        bool _rescheduling { false };

        bool notify() noexcept;

        /**
         * @brief Deadlock resolution helper
         * @details Cutex can be deadlocked after an unexpected syscall of the process timeout.
         * The halper figures this out <br><br>
         * @b Cutex deadlock case:
         * - thread @b A owns @b cutex
         * - thread @b B tries to capture @b cutex but didn't receive success.
         * - thread @b B going to sign up into @b cutex @b waiters queue
         * - OS interrupts thread @b B, before it signed up.
         * - thread @b A making @b cutex sync operation
         * - @b cutex @b waiters queue is empty (thread @b B didn't finish signing up before interruption) notify noone
         * - @b cutex is vacant but @b B thread isn't notified and waits inside @b cutex @b waiters queue
         * - Got deadlock (notify sequence broken) @b B thread is forever blocked
         */
        async<> pending_notify() noexcept;

        bool try_lock() noexcept;

        bool await_ready() override { return try_lock(); }

        bool await_suspend(auto coroutine);

        // ReSharper disable once CppMemberFunctionMayBeStatic
        void await_resume() {}

        ~cutex_future() override = default;

        void set_rescheduling(const bool rs) noexcept { _rescheduling = rs; }

        [[nodiscard]] bool get_rescheduling() const noexcept { return _rescheduling; }
    };

    // NOTE: <C>ooperative <U>serspace mu<TEX>
    class cutex final : protected cutex_future {

        [[nodiscard]] auto capture() noexcept -> cutex_future&;

        void sync() noexcept;

        class proxy;

    public:

        cutex() = default;

        cutex(const cutex&) = delete;

        cutex(cutex&&) = delete;

        typedef volatile proxy vol_proxy;

        ~cutex() override = default;

        using cutex_future::set_rescheduling;

        using cutex_future::get_rescheduling;
    };

    class cutex::proxy {

        cutex& _cutex;
        bool _is_synced { true };

    public:

        proxy() = delete;

        proxy(const proxy&) = delete;

        proxy(proxy&&) = delete;

        explicit proxy(cutex& cx) : _cutex(cx) { }

        [[nodiscard]] auto capture() volatile -> cutex_future& {
            if (not _is_synced)
                throw std::logic_error {"cutex 'capture()' before 'sync()'"};
            _is_synced = false;
            return _cutex.capture();
        };

        void sync() volatile noexcept { if (not _is_synced) { _cutex.sync(); _is_synced = true; } };

        ~proxy() { sync(); }
    };

} // end namespace ace::futures

namespace ace {
    using futures::cutex;
    using croxy = cutex::vol_proxy;
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
        _cutex->_waiters.push(std::move(ctx));
    }

    // TODO: Finish later
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
    // NOTE: Trying to fetch next waiter and release it on the runner
    if (async<> waiter; _waiters.pop(waiter)) {
        // NOTE: Updating rescheduling pool if rescheduling mode is on and waiter forbids roaming
        if (const bool roaming = waiter._coroutine.promise()._roaming; _rescheduling and not roaming)
            _runner_pool.store(waiter._coroutine.promise()._runner_pool, std::memory_order_release);
        // NOTE: Rescheduling waiter if rescheduling mode is on and waiter supports roaming
        else if (_rescheduling)
            waiter._coroutine.promise()._runner_pool = _runner_pool.load(std::memory_order_acquire);
        waiter.release_future();
        core::runner::reattach(std::move(waiter));
        return true;
    }
    return false;
}

ACE_FUTURE_CUTEX_FUTURE_MEMBER(ace::async<>)
pending_notify() noexcept {
    while (true) {
        if (notify()) co_return;
        // NOTE: If notify still has no success, suspend and retry
        co_await suspend();
    }
}

ACE_FUTURE_CUTEX_FUTURE_MEMBER(bool)
await_suspend(auto coroutine) {
    // NOTE: Selecting rescheduling pool if it doesn't set and rescheduling mode on
    if (not _runner_pool.load(std::memory_order_acquire))
        _runner_pool.store(coroutine.promise()._runner_pool, std::memory_order_release);

    // NOTE: Setting conductor for dispatch to the cutex waiters queue
    coroutine.promise()._future_conductor = cutex_conductor{this};
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
