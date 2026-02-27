#ifndef ACE_FUTURE_CUTEX_H
#define ACE_FUTURE_CUTEX_H
#include "future.h"
#include "ace/core/runner.h"
#include "ace/core/disruptor.h"
#include "ace/coroutines/context.h"
#include "nukes/dynamic/mpmc_queue.h"


namespace ace::futures {

    enum class cutex_state : uint8_t {
        e_vacant,
        e_captured,
        e_pending,
    };


    class cutex_future : public future_traits<cutex_future> {

        struct cutex_conductor;
        friend cutex_conductor;

    public:

        DECLARE_FUTURE(cutex_future)
        IMPORT_FUTURE_ENV

        std::atomic<cutex_state> _state {cutex_state::e_vacant };
        nukes::dynamic::mpmc_queue<async<>> _waiters {};
        std::atomic<int> _users { 0 };

        bool try_lock() noexcept;

        bool await_ready() override { return try_lock(); }

        bool await_suspend(auto coroutine);

        void await_resume() {}

        ~cutex_future() override = default;
    };

    // NOTE: Cooperative Userspace muTEX
    class cutex final : protected cutex_future {

        friend class cutex_future;
        friend class core::disruptor;
        friend class cutex_wrap;

        void resolve() noexcept;

        void add_user() { if (_users.fetch_add(1, std::memory_order_release) == 1) core::disruptor::attach_cutex(this); }

        void del_user() { _users.fetch_sub(1, std::memory_order_release); }

        [[nodiscard]] auto capture() noexcept -> cutex_future&;

        [[nodiscard]] bool try_capture() noexcept;

        void sync() noexcept;

        [[nodiscard]] bool is_attached() const noexcept { return _users.load(std::memory_order_acquire) > 0; };

    public:

        cutex() = default;

        cutex(const cutex&) = delete;

        cutex(cutex&&) = delete;

        ~cutex() override = default;

        class proxy;
    };

    class cutex::proxy {

        cutex& _cutex;
        bool _is_synced { true };

    public:

        proxy() = delete;

        proxy(const proxy&) = delete;

        proxy(proxy&&) = delete;

        explicit proxy(cutex& cx) : _cutex(cx) { _cutex.add_user(); }

        [[nodiscard]] auto capture() -> cutex_future& {
            if (not _is_synced)
                throw std::logic_error {"cutex 'capture()' before 'sync()'"};
            _is_synced = false;
            return _cutex.capture();
        };

        void sync() noexcept { if (not _is_synced) { _cutex.sync(); _is_synced = true; } };

        ~proxy() { sync(); _cutex.del_user(); }
    };

} // end namespace ace::futures

namespace ace {
    using futures::cutex;
    using croxy = cutex::proxy;
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
    auto state = _state.load(std::memory_order_acquire);
    return (state == cutex_state::e_vacant or state == cutex_state::e_pending)
        and _state.compare_exchange_weak(state, cutex_state::e_captured,
            std::memory_order_release, std::memory_order_relaxed);
}

ACE_FUTURE_CUTEX_FUTURE_MEMBER(bool)
await_suspend(auto coroutine) {
    if (try_lock()) return false;
    coroutine.promise()._future_conductor = cutex_conductor{this};
    return true;
}

#undef ACE_FUTURE_CUTEX_FUTURE_MEMBER
#undef ACE_FUTURE_CUTEX_FUTURE_SPACE

#define ACE_FUTURE_CUTEX_SPACE \
ace::futures::cutex::

#define ACE_FUTURE_CUTEX_MEMBER(returnT) \
returnT ACE_FUTURE_CUTEX_SPACE


ACE_FUTURE_CUTEX_MEMBER(void)
resolve() noexcept {
    auto state = _state.load(std::memory_order_acquire);
    // NOTE: Checking if state is vacant and trying to pull waiter
    if (async<> _waiter; state == cutex_state::e_vacant and _waiters.pop(_waiter)) {
        core::runner::reattach(std::move(_waiter));
        _state.compare_exchange_weak(state, cutex_state::e_pending,
            std::memory_order_release, std::memory_order_acquire);
    }
}

ACE_FUTURE_CUTEX_MEMBER(ace::futures::cutex_future&)
capture() noexcept { return *static_cast<cutex_future*>(this); }

ACE_FUTURE_CUTEX_MEMBER(bool)
try_capture() noexcept {
    return try_lock();
}

ACE_FUTURE_CUTEX_MEMBER(void)
sync() noexcept {
    if (_state.load(std::memory_order_acquire) not_eq cutex_state::e_captured)
        return;
    if (async<> _waiter; _waiters.pop(_waiter)) [[likely]] {
        _state.store(cutex_state::e_pending, std::memory_order_release);
        core::runner::reattach(std::move(_waiter));
    } else
        _state.store(cutex_state::e_vacant, std::memory_order_release);
}

#undef ACE_FUTURE_CUTEX_MEMBER
#undef ACE_FUTURE_CUTEX_SPACE

inline void ace::core::disruptor::resolve(cutex* cutex_) noexcept { cutex_->resolve(); }

inline bool ace::core::disruptor::is_detached(const cutex* cutex_) noexcept { return not cutex_->is_attached(); };

inline bool ace::core::disruptor::is_empty_cutex(cutex* cutex_) noexcept {
    return  cutex_->_users.load(std::memory_order_acquire) == 0
        and cutex_->_state.load(std::memory_order_acquire) == futures::cutex_state::e_vacant
        and cutex_->_waiters.empty();
}

#endif //ACE_FUTURE_CUTEX_H
