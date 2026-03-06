#ifndef ACE_FUTURE_CUTEX_H
#define ACE_FUTURE_CUTEX_H
#include "future.h"
#include "ace/core/runner.h"
#include "ace/core/disruptor.h"
#include "ace/coroutines/context.h"
#include "nukes/dynamic/mpsc_queue.h"


namespace ace::futures {

    class cutex_future : public future_traits<cutex_future> {

        struct cutex_conductor;
        friend cutex_conductor;

    public:

        DECLARE_FUTURE(cutex_future)
        IMPORT_FUTURE_ENV

        // NOTE: <int> instead of <uint64_t> because unsigned type may ruin process on overflow after subtract
        std::atomic<int> _users { 0 };
        nukes::dynamic::mpsc_queue<async<>> _waiters {};
        std::atomic<runner_pool_t*> _runner_pool { nullptr };
        bool _rescheduling { false };

        bool try_lock() noexcept;

        bool await_ready() override { return false; }

        // bool await_ready() override { return try_lock(); }

        bool await_suspend(auto coroutine);

        // ReSharper disable once CppMemberFunctionMayBeStatic
        void await_resume() {}

        ~cutex_future() override = default;

        void set_rescheduling(const bool rs) noexcept { _rescheduling = rs; }

        [[nodiscard]] bool get_rescheduling() const noexcept { return _rescheduling; }
    };

    // NOTE: <C>ooperative <U>serspace mu<TEX>
    class cutex final : protected cutex_future {

        friend class core::disruptor;

        bool resolve() noexcept;

        [[nodiscard]] auto capture() noexcept -> cutex_future&;

        void sync() noexcept;

    public:

        cutex() = default;

        cutex(const cutex&) = delete;

        cutex(cutex&&) = delete;

        ~cutex() override = default;

        class proxy;

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

        [[nodiscard]] auto capture() -> cutex_future& {
            if (not _is_synced)
                throw std::logic_error {"cutex 'capture()' before 'sync()'"};
            _is_synced = false;
            return _cutex.capture();
        };

        void sync() noexcept { if (not _is_synced) { _cutex.sync(); _is_synced = true; } };

        ~proxy() { sync(); }
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
    return _users.fetch_add(1, std::memory_order_acq_rel) == 0;
}

ACE_FUTURE_CUTEX_FUTURE_MEMBER(bool)
await_suspend(auto coroutine) {
    const bool captured = try_lock();
    const bool on_reschedule = not captured and _rescheduling and coroutine.promise()._roaming;

    // NOTE: Selecting rescheduling pool if it doesn't set
    if (_rescheduling and not _runner_pool.load(std::memory_order_acquire))
        _runner_pool.store(coroutine.promise()._runner_pool, std::memory_order_release);

    // NOTE: Leaving because cutex captured
    if (captured) return false;

    // NOTE: Setting a new pool for the task if it is allowed
    if (on_reschedule)
        coroutine.promise()._runner_pool = _runner_pool.load(std::memory_order_acquire);

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


ACE_FUTURE_CUTEX_MEMBER(bool)
resolve() noexcept {
    // NOTE: Trying to fetch and release from cutex waiters queue
    if (async<> _waiter; _waiters.pop(_waiter)) {
        _waiter.release_future();
        core::runner::reattach(std::move(_waiter));
        return true;
    }
    return false;
}

ACE_FUTURE_CUTEX_MEMBER(ace::futures::cutex_future&)
capture() noexcept { return *static_cast<cutex_future*>(this); }

ACE_FUTURE_CUTEX_MEMBER(void)
sync() noexcept {
    // NOTE: Subtract users because leaving cutex
    const bool has_waiters = _users.fetch_sub(1, std::memory_order_acq_rel) > 1;
    // NOTE: Trying to fetch next waiter and release it on the runner
    if (async<> waiter; has_waiters and _waiters.pop(waiter)) {
        waiter.release_future();
        core::runner::reattach(std::move(waiter));
    }
    // NOTE: If there are some waiters but fetching is failed
    // NOTE: than requesting resolve from disruptor
    else if (has_waiters) core::disruptor::request_resolve(this);
}

#undef ACE_FUTURE_CUTEX_MEMBER
#undef ACE_FUTURE_CUTEX_SPACE

inline bool ace::core::disruptor::resolve(cutex* cutex_) noexcept { return cutex_->resolve(); }

#endif //ACE_FUTURE_CUTEX_H
