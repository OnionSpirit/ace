#ifndef ACE_FUTURE_TIMEOUT_H
#define ACE_FUTURE_TIMEOUT_H

#include <future>

#include "future.h"
#include "ace/coroutines/context.h"
#include "ace/core/clock.h"

using namespace std::chrono_literals;

namespace ace::futures {


class timeout : public busy_future_traits<timeout> {

    core::duration_t _duration;
    bool _released {false};

    struct timeout_conductor;
    friend timeout_conductor;

    public:

        IMPORT_BUSY_FUTURE_ENV(timeout)

        template <typename I, typename T>
        requires std::is_integral_v<I>
        explicit timeout(std::chrono::duration<I, T> t) {
            _duration = std::chrono::duration_cast<std::chrono::milliseconds, uint64_t, std::milli>(t);
        };

        bool await_ready() override { return _released; }

        bool await_suspend(auto coroutine);

        void await_resume() {}

        void reset() { _released = false; }

        // TODO: Support detatch later
        // void detach() { _detached = true; }
        //
        // bool is_detached() { return _detached;}
};

struct expire : timeout {
    explicit expire(core::timepoint_t expires)
        : timeout(expires - core::clock::current_time()) {}
};

} // end namespace ace::futures


//==============================- DEFINITIONS -==================================


#define ACE_FUTURE_TIMEOUT_SPACE \
ace::futures::timeout::

#define ACE_FUTURE_TIMEOUT_MEMBER(returnT) \
returnT ACE_FUTURE_TIMEOUT_SPACE

struct ACE_FUTURE_TIMEOUT_SPACE timeout_conductor : conductor_handler_t {

    timeout_conductor() = delete;

    explicit timeout_conductor(timeout* timeout_)
        : _timeout(timeout_) {};

    void forward(async<>&& ctx) override {
        // NOTE: Marking timeout released.
        // NOTE: Because await_ready() will be called after context retreatment to runner.
        // NOTE: And retreatment will happen only when timeout actually expired
        _timeout->_released = true;
        _injected_node = core::clock::subscribe(std::move(ctx), _timeout->_duration);
    }

    void cancel() override {
        if (_injected_node)
            core::clock::detach(_injected_node);
    }

    ~timeout_conductor() override = default;

    core::clock_node* _injected_node = nullptr;
    timeout* const _timeout;
};


ACE_FUTURE_TIMEOUT_MEMBER(bool)
await_suspend(auto coroutine) {
    coroutine.promise()._runner_conductor = timeout_conductor{this};
    return true;
}

#undef ACE_FUTURE_TIMEOUT_MEMBER
#undef ACE_FUTURE_TIMEOUT_SPACE
#endif // ACE_FUTURE_TIMEOUT_H
