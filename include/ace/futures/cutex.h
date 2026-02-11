#ifndef ACE_FUTURE_CUTEX_H
#define ACE_FUTURE_CUTEX_H
#include "future.h"
#include "ace/core/runner.h"
#include "ace/core/fixer.h"
#include "ace/coroutines/context.h"
#include "nukes/dynamic/mpmc_queue.h"


namespace ace::futures {

    enum class cutx_state : uint8_t {
        e_free,
        e_captured,
        e_pending,
    };

    class cutex_locker : public future_traits<cutex_locker> {

        struct cutex_conductor;
        friend cutex_conductor;

    public:

        DECLARE_FUTURE(cutex_locker)
        IMPORT_FUTURE_ENV

        bool try_lock() noexcept;

        bool await_ready() override { return try_lock(); }

        bool await_suspend(auto coroutine);

        static void await_resume() {}

        std::atomic<cutx_state> _state {cutx_state::e_free };

        // std::atomic_flag _captured = ATOMIC_FLAG_INIT;
        // std::atomic<bool> _pending {};

        mutable nukes::dynamic::mpmc_queue<async<>> _waiters {};

        ~cutex_locker() override = default;
    };

    // NOTE: Cooperative Userspace muTEX
    class cutex : protected cutex_locker {

        friend class cutex_locker;
        friend core::fixer;

        bool resolve() noexcept;

        public:

        cutex() { core::fixer::attach_cutex(this); }

        ~cutex() override = default;

        auto capture() noexcept -> cutex_locker&;

        bool try_capture() noexcept;

        void sync() noexcept;
    };

} // end namespace ace::futures

//==============================- DEFINITIONS -==================================

#define ACE_FUTURE_CUTEX_LOCKER_SPACE \
ace::futures::cutex_locker::

#define ACE_FUTURE_CUTEX_LOCKER_MEMBER(returnT) \
returnT ACE_FUTURE_CUTEX_LOCKER_SPACE


struct ACE_FUTURE_CUTEX_LOCKER_SPACE cutex_conductor : conductor_handler_t {

    cutex_conductor() = delete;

    explicit cutex_conductor(const cutex_locker* cutex_)
        : _cutex(cutex_) {};

    void forward(async<>&& ctx) override {
        _cutex->_waiters.push(std::move(ctx));
    }

    ~cutex_conductor() override = default;

    const cutex_locker* _cutex;
};

ACE_FUTURE_CUTEX_LOCKER_MEMBER(bool)
try_lock() noexcept {
    auto state = _state.load(std::memory_order_acquire);
    return (state == cutx_state::e_free or state == cutx_state::e_pending)
        and _state.compare_exchange_weak(state, cutx_state::e_captured,
            std::memory_order_release, std::memory_order_relaxed);
}

ACE_FUTURE_CUTEX_LOCKER_MEMBER(bool)
await_suspend(auto coroutine) {
    if (try_lock()) return false;
    // TODO: Remove timeout line after mtx_resolve_service will be tested
    std::this_thread::sleep_for(1ms);
    coroutine.promise()._conductor = cutex_conductor{this};
    return true;
}

#undef ACE_FUTURE_CUTEX_LOCKER_MEMBER
#undef ACE_FUTURE_CUTEX_LOCKER_SPACE

#define ACE_FUTURE_CUTEX_SPACE \
ace::futures::cutex::

#define ACE_FUTURE_CUTEX_MEMBER(returnT) \
returnT ACE_FUTURE_CUTEX_SPACE


ACE_FUTURE_CUTEX_MEMBER(bool)
resolve() noexcept {
    bool state_changed { false };
    auto state = _state.load(std::memory_order_acquire);

    // NOTE: Checking if state is 'free'
    if (state == cutx_state::e_free) [[unlikely]] {
        // NOTE: Using 'weak' version because 'strong' option consumes more time
        // NOTE: and logic wont break if haven't captured 'free' state
        state_changed = _state.compare_exchange_weak(state, cutx_state::e_pending,
            std::memory_order_release, std::memory_order_relaxed);
        state = cutx_state::e_pending;
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
        _state.compare_exchange_strong(state, cutx_state::e_free,
            std::memory_order_release, std::memory_order_relaxed);
    }
    return false;
}

ACE_FUTURE_CUTEX_MEMBER(ace::futures::cutex_locker&)
capture() noexcept {
    return *static_cast<cutex_locker*>(this);
}

ACE_FUTURE_CUTEX_MEMBER(bool)
try_capture() noexcept {
    return try_lock();
}

ACE_FUTURE_CUTEX_MEMBER(void)
sync() noexcept {
    if (_state.load(std::memory_order_acquire) not_eq cutx_state::e_captured)
        return;
    if (async<> _waiter; _waiters.pop(_waiter)) [[likely]] {
        _state.store(cutx_state::e_pending, std::memory_order_release);
        core::runner::reattach(std::move(_waiter));
    } else {
        _state.store(cutx_state::e_free, std::memory_order_release);
        core::fixer::attach_cutex(this);
    }
}

#undef ACE_FUTURE_CUTEX_MEMBER
#undef ACE_FUTURE_CUTEX_SPACE

inline bool ace::core::fixer::resolve(futures::cutex* cutex_) noexcept { return cutex_->resolve(); }
inline bool ace::core::fixer::is_empty_cutex(const futures::cutex* cutex_) noexcept { return cutex_->_waiters.empty(); }

#endif //ACE_FUTURE_CUTEX_H
