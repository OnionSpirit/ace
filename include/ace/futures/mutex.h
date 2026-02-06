#ifndef ACE_FUTURE_MUTEX_H
#define ACE_FUTURE_MUTEX_H
#include "future.h"
#include "ace/core/runner.h"
#include "ace/coroutines/context.h"
#include "nukes/dynamic/mpmc_queue.h"

// NOTE: Predefine mutex resolve service
// namespace ace::core {
//     struct core::resolve_sevice;
// }

namespace ace::futures {

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

        std::atomic_flag _captured = ATOMIC_FLAG_INIT;
        mutable nukes::dynamic::mpmc_queue<async<>> _waiters {};
    };

    class mutex : protected mutex_locker {

        void notify() noexcept;

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
    // NOTE: _captured.test() == false, means that mutex is free.
    // NOTE:  _captured.test_and_set() == false, means that mutex capture succeed
    return not (_captured.test() or _captured.test_and_set(std::memory_order_acq_rel));
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


ACE_FUTURE_MUTEX_MEMBER(void)
notify() noexcept {
    if (async<> _waiter; _waiters.pop(_waiter)) [[likely]]
        core::runner::schedule(std::move(_waiter));
}

ACE_FUTURE_MUTEX_MEMBER(void)
resolve() noexcept {
    if (_captured.test(std::memory_order_acquire) == false)
        notify();
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
    _captured.clear(std::memory_order_release);
    notify();
}

#undef ACE_FUTURE_MUTEX_MEMBER
#undef ACE_FUTURE_MUTEX_SPACE

#endif //ACE_FUTURE_MUTEX_H