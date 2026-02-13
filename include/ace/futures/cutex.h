#ifndef ACE_FUTURE_CUTEX_H
#define ACE_FUTURE_CUTEX_H
#include "future.h"
#include "ace/core/runner.h"
#include "ace/core/fixer.h"
#include "ace/coroutines/context.h"
#include "nukes/dynamic/mpmc_queue.h"


namespace ace::futures {

    enum class cutex_state : uint8_t {
        e_vacant,
        e_captured,
        e_pending,
    };

    struct cutex_core {
        std::atomic<cutex_state> _state {cutex_state::e_vacant };
        nukes::dynamic::mpmc_queue<async<>> _waiters {};
    };

    class cutex_future : public future_traits<cutex_future> {

        struct cutex_conductor;
        friend cutex_conductor;

    public:

        DECLARE_FUTURE(cutex_future)
        IMPORT_FUTURE_ENV

        bool try_lock() noexcept;

        bool await_ready() override { return try_lock(); }

        bool await_suspend(auto coroutine);

        void await_resume() {}

        std::shared_ptr<cutex_core> _core = std::make_shared<cutex_core>();

        ~cutex_future() override = default;
    };

    // NOTE: Cooperative Userspace muTEX
    class cutex final : protected cutex_future {

        friend class cutex_future;
        friend core::fixer;

        bool resolve() noexcept;

    public:

        cutex() = default;

        ~cutex() override = default;

        [[nodiscard]] auto capture() noexcept -> cutex_future&;

        [[nodiscard]] bool try_capture() noexcept;

        void sync() noexcept;
    };

    class secure_capture {

        cutex& _cutex;

    public:

        secure_capture() = delete;

        explicit secure_capture(cutex& cx) : _cutex(cx) {}

        [[nodiscard]] auto capture() const noexcept -> cutex_future& { return _cutex.capture(); };

        void sync() const noexcept { _cutex.sync(); };

        ~secure_capture() { sync(); }
    };

} // end namespace ace::futures

namespace ace {
    using futures::cutex;
    using futures::secure_capture;
}

//==============================- DEFINITIONS -==================================

#define ACE_FUTURE_CUTEX_FUTURE_SPACE \
ace::futures::cutex_future::

#define ACE_FUTURE_CUTEX_FUTURE_MEMBER(returnT) \
returnT ACE_FUTURE_CUTEX_FUTURE_SPACE


struct ACE_FUTURE_CUTEX_FUTURE_SPACE cutex_conductor : conductor_handler_t {

    cutex_conductor() = delete;

    explicit cutex_conductor(const cutex_future* cutex_)
        : _cutex(cutex_) {};

    void forward(async<>&& ctx) override {
        _cutex->_core->_waiters.push(std::move(ctx));
    }

    ~cutex_conductor() override = default;

    const cutex_future* _cutex;
};

ACE_FUTURE_CUTEX_FUTURE_MEMBER(bool)
try_lock() noexcept {
    auto state = _core->_state.load(std::memory_order_acquire);
    return (state == cutex_state::e_vacant or state == cutex_state::e_pending)
        and _core->_state.compare_exchange_weak(state, cutex_state::e_captured,
            std::memory_order_release, std::memory_order_relaxed);
}

ACE_FUTURE_CUTEX_FUTURE_MEMBER(bool)
await_suspend(auto coroutine) {
    if (try_lock()) return false;
    // // TODO: Remove timeout line after mtx_resolve_service will be tested
    // std::this_thread::sleep_for(1ms);
    coroutine.promise()._conductor = cutex_conductor{this};
    return true;
}

#undef ACE_FUTURE_CUTEX_FUTURE_MEMBER
#undef ACE_FUTURE_CUTEX_FUTURE_SPACE

#define ACE_FUTURE_CUTEX_SPACE \
ace::futures::cutex::

#define ACE_FUTURE_CUTEX_MEMBER(returnT) \
returnT ACE_FUTURE_CUTEX_SPACE


ACE_FUTURE_CUTEX_MEMBER(bool)
resolve() noexcept {
    bool state_changed { false };
    auto state = _core->_state.load(std::memory_order_acquire);

    // NOTE: Checking if state is 'free'
    if (state == cutex_state::e_vacant) [[unlikely]] {
        // NOTE: Using 'weak' version because 'strong' option consumes more time
        // NOTE: and logic wont break if haven't captured 'free' state
        state_changed = _core->_state.compare_exchange_weak(state, cutex_state::e_pending,
            std::memory_order_release, std::memory_order_relaxed);
        state = cutex_state::e_pending;
    } else return false;

    // NOTE: If we have changed the state then trying to pull and reattach next waiter
    if (async<> _waiter; state_changed and _core->_waiters.pop(_waiter)) [[unlikely]] {
        core::runner::reattach(std::move(_waiter));
        return true;
    }

    // NOTE: If we didn't pull waiter successfully but state changed,
    // NOTE: then restoring state to 'free' if it wasn't changed
    if (state_changed) [[unlikely]] {
        // NOTE: Using 'strong' version because 'weak' one may skip equality (state == e_free)
        _core->_state.compare_exchange_strong(state, cutex_state::e_vacant,
            std::memory_order_release, std::memory_order_relaxed);
    }
    return false;
}

ACE_FUTURE_CUTEX_MEMBER(ace::futures::cutex_future&)
capture() noexcept {
    return *static_cast<cutex_future*>(this);
}

ACE_FUTURE_CUTEX_MEMBER(bool)
try_capture() noexcept {
    return try_lock();
}

ACE_FUTURE_CUTEX_MEMBER(void)
sync() noexcept {
    if (_core->_state.load(std::memory_order_acquire) not_eq cutex_state::e_captured)
        return;
    if (async<> _waiter; _core->_waiters.pop(_waiter)) [[likely]] {
        _core->_state.store(cutex_state::e_pending, std::memory_order_release);
        core::runner::reattach(std::move(_waiter));
    } else {
        _core->_state.store(cutex_state::e_vacant, std::memory_order_release);
        core::fixer::attach_cutex(this);
    }
}

#undef ACE_FUTURE_CUTEX_MEMBER
#undef ACE_FUTURE_CUTEX_SPACE

inline bool ace::core::fixer::resolve(futures::cutex* cutex_) noexcept { return cutex_->resolve(); }

inline bool ace::core::fixer::is_empty_cutex(const futures::cutex* cutex_) noexcept {
    return cutex_->_core->_waiters.empty()
        and cutex_->_core->_state.load(std::memory_order_acquire) == futures::cutex_state::e_vacant;
}

#endif //ACE_FUTURE_CUTEX_H
