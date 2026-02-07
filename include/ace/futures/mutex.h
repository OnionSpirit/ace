#ifndef ACE_FUTURE_MUTEX_H
#define ACE_FUTURE_MUTEX_H
#include "future.h"
#include "ace/core/runner.h"
#include "ace/coroutines/context.h"
#include "nukes/dynamic/mpmc_queue.h"


namespace ace::futures {

    enum mutex_state {
        e_free, // NOTE: There are no task that waiting or captured mutex
        e_locked, // NOTE: Mutex is locked
        e_pending, // NOTE: There is task that was released but didn't capture mutex yet
    };

    class mutex_locker : public future_traits<mutex_locker> {

        struct mutex_conductor;
        friend mutex_conductor;

    public:

        DECLARE_FUTURE(mutex_locker)
        IMPORT_FUTURE_ENV

        bool await_ready() override { return basic_capture(); }

        bool await_suspend(auto coroutine);

        static void await_resume() {}

        bool basic_capture() noexcept;

        std::atomic<std::size_t> _candidate_id {};
        std::atomic<mutex_state> _captured { e_free };
        mutable nukes::dynamic::mpmc_queue<async<>> _waiters {};
    };

    class mutex : protected mutex_locker {

        bool notify_one() noexcept;

        void resolve() noexcept;

        // friend core::resolve_service;

        friend class mutex_locker;

        public:

        auto capture() noexcept -> const mutex_locker &;

        bool try_capture() noexcept;

        void sync() noexcept;
    };

} // end namespace ace::futures

//==============================- DEFINITIONS -==================================

#define ACE_FUTURE_MUTEX_LOCKER_SPACE \
ace::futures::mutex_locker::

#define ACE_FUTURE_MUTEX_LOCKER_MEMBER(returnT) \
returnT ACE_FUTURE_MUTEX_LOCKER_SPACE


struct ACE_FUTURE_MUTEX_LOCKER_SPACE mutex_conductor : conductor_handler_t {

    mutex_conductor() = delete;

    explicit mutex_conductor(const mutex_locker* mutex_)
        : _mutex(mutex_) {};

    void forward(async<>&& ctx) override {
        _mutex->_waiters.push(std::move(ctx));
    }

    ~mutex_conductor() override = default;

    const mutex_locker* _mutex;
};

ACE_FUTURE_MUTEX_LOCKER_MEMBER(bool)
basic_capture() noexcept {
    auto current_lock = _captured.load(std::memory_order_relaxed);
    return (current_lock == e_free or current_lock == e_pending)
    and _captured.compare_exchange_weak(
        current_lock,
        e_locked,
        std::memory_order_release,
        std::memory_order_relaxed
    );
}

ACE_FUTURE_MUTEX_LOCKER_MEMBER(bool)
await_suspend(auto coroutine) {
    coroutine.promise()._conductor = mutex_conductor{this};
    return true;
}

#undef ACE_FUTURE_MUTEX_LOCKER_MEMBER
#undef ACE_FUTURE_MUTEX_LOCKER_SPACE

#define ACE_FUTURE_MUTEX_SPACE \
ace::futures::mutex::

#define ACE_FUTURE_MUTEX_MEMBER(returnT) \
returnT ACE_FUTURE_MUTEX_SPACE


ACE_FUTURE_MUTEX_MEMBER(bool)
notify_one() noexcept {
    if (async<> _waiter; _waiters.pop(_waiter)) [[likely]] {
        _captured.store(e_pending, std::memory_order_release);
        core::runner::schedule(std::move(_waiter));
        return true; // NOTE: One notified
    }
    return false; // NOTE: Noone is notified
}

ACE_FUTURE_MUTEX_MEMBER(void)
resolve() noexcept {
    // NOTE: Storing current mutex state
    auto current_lock = _captured.load(std::memory_order_relaxed);

    // NOTE: If lock is free and not empty waiters queue and we successfully changed status to pending.
    // NOTE: It means we have smth to resolve
    const bool is_on_resolve {
        current_lock == e_free
        and not _waiters.empty()
        and _captured.compare_exchange_weak(
            current_lock,
            e_pending,
            std::memory_order_release,
            std::memory_order_relaxed
        )
    };

    // NOTE: If we successfully fetched waiter, we scheduling it. Else we are trying to restore mutex status
    if (async<> _waiter; is_on_resolve and _waiters.pop(_waiter)) [[unlikely]] {
        core::runner::schedule(std::move(_waiter));
    } else if (is_on_resolve) {
        _captured.compare_exchange_weak(
            current_lock,
            e_free,
            std::memory_order_release,
            std::memory_order_relaxed
        );
    }
}

ACE_FUTURE_MUTEX_MEMBER(const ace::futures::mutex_locker&)
capture() noexcept {
    return *static_cast<mutex_locker*>(this);
}

ACE_FUTURE_MUTEX_MEMBER(bool)
try_capture() noexcept {
    return basic_capture();
}

ACE_FUTURE_MUTEX_MEMBER(void)
sync() noexcept {
    if (async<> _waiter; _waiters.pop(_waiter)) [[likely]] {
        _captured.store(e_pending, std::memory_order_release);
        core::runner::schedule(std::move(_waiter));
    } else _captured.store(e_free, std::memory_order_release);
}

#undef ACE_FUTURE_MUTEX_MEMBER
#undef ACE_FUTURE_MUTEX_SPACE

#endif //ACE_FUTURE_MUTEX_H
