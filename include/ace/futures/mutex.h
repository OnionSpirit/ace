#ifndef ACE_FUTURE_MUTEX_H
#define ACE_FUTURE_MUTEX_H
#include "future.h"
#include "ace/core/runner.h"
#include "ace/coroutines/context.h"
#include "nukes/dynamic/mpmc_queue.h"


namespace ace::futures {

    enum class mtx_state : uint8_t{
        e_free,
        e_captured,
        e_pending,
    };

    class mutex_locker : public future_traits<mutex_locker> {

        struct mutex_conductor;
        friend mutex_conductor;

    public:

        DECLARE_FUTURE(mutex_locker)
        IMPORT_FUTURE_ENV

        bool try_lock() noexcept;

        bool await_ready() override { return try_lock(); }

        bool await_suspend(auto coroutine);

        static void await_resume() {}

        std::atomic<mtx_state> _state {mtx_state::e_free };

        // std::atomic_flag _captured = ATOMIC_FLAG_INIT;
        // std::atomic<bool> _pending {};

        mutable nukes::dynamic::mpmc_queue<async<>> _waiters {};
    };

    class mutex : protected mutex_locker {

        // friend core::resolve_service;

        friend class mutex_locker;

        public:

        bool resolve() noexcept;

        auto capture() noexcept -> mutex_locker&;

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
try_lock() noexcept {
    auto state = _state.load(std::memory_order_acquire);
    return (state == mtx_state::e_free or state == mtx_state::e_pending)
        and _state.compare_exchange_weak(state, mtx_state::e_captured,
            std::memory_order_release, std::memory_order_relaxed);
}

ACE_FUTURE_MUTEX_LOCKER_MEMBER(bool)
await_suspend(auto coroutine) {
    // TODO: Remove timeout line after mtx_resolve_service will be tested
    std::this_thread::sleep_for(1ms);
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
resolve() noexcept {
    bool state_changed { false };
    auto state = _state.load(std::memory_order_acquire);

    // NOTE: Checking if state is 'free'
    if (state == mtx_state::e_free) [[unlikely]] {
        // NOTE: Using 'weak' version because 'strong' option consumes more time
        // NOTE: and logic wont break if haven't captured 'free' state
        state_changed = _state.compare_exchange_weak(state, mtx_state::e_pending,
            std::memory_order_release, std::memory_order_relaxed);
        state = mtx_state::e_pending;
    } else return false;

    // NOTE: If we have changed the state then trying to pull and reattach next waiter
    if (async<> _waiter; state_changed and _waiters.pop(_waiter)) [[unlikely]] {
        core::runner::reattach(std::move(_waiter));
        return true;
    }

    // NOTE: If we didn't pull waiter successfully but state changed,
    // NOTE: then restoring state to 'free' if it wasn't changed
    if (state_changed) [[unlikely]] {
        // NOTE: Using 'strong' version because 'weak' one may skip equality (state == e_free)
        _state.compare_exchange_strong(state, mtx_state::e_free,
            std::memory_order_release, std::memory_order_relaxed);
    }
    return false;
}

ACE_FUTURE_MUTEX_MEMBER(ace::futures::mutex_locker&)
capture() noexcept {
    return *static_cast<mutex_locker*>(this);
}

ACE_FUTURE_MUTEX_MEMBER(bool)
try_capture() noexcept {
    return try_lock();
}

ACE_FUTURE_MUTEX_MEMBER(void)
sync() noexcept {
    if (_state.load(std::memory_order_acquire) not_eq mtx_state::e_captured)
        return;
    if (async<> _waiter; _waiters.pop(_waiter)) [[likely]] {
        _state.store(mtx_state::e_pending, std::memory_order_release);
        core::runner::reattach(std::move(_waiter));
    } else {
        _state.store(mtx_state::e_free, std::memory_order_release);
    }
}

#undef ACE_FUTURE_MUTEX_MEMBER
#undef ACE_FUTURE_MUTEX_SPACE

#endif //ACE_FUTURE_MUTEX_H
