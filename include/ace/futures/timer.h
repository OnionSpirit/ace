#ifndef ACE_FUTURE_TIMER_H
#define ACE_FUTURE_TIMER_H

#include <future>

#include "future.h"
#include "ace/common/selection.h"
#include "ace/coroutines/context.h"
#include "ace/core/clock.h"

using namespace std::chrono_literals;

namespace ace::futures {


class timer : public future_traits<timer> {

    core::duration_t _duration;
    bool _released {false};

    struct timer_conductor;
    friend timer_conductor;

    public:

        DECLARE_FUTURE(timer)
        IMPORT_FUTURE_ENV

        template <typename I, typename T>
        requires std::is_integral_v<I>
        explicit timer(std::chrono::duration<I, T> t) {
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

struct expire : timer {
    explicit expire(core::timepoint_t expires)
        : timer(expires - core::clock::current_time()) {}
};

} // end namespace ace::futures


//==============================- DEFINITIONS -==================================


#define ACE_FUTURE_TIMER_SPACE \
ace::futures::timer::

#define ACE_FUTURE_TIMER_MEMBER(returnT) \
returnT ACE_FUTURE_TIMER_SPACE

struct ACE_FUTURE_TIMER_SPACE timer_conductor : conductor_handler_t {

    timer_conductor() = delete;

    explicit timer_conductor(timer* timer_)
        : _timer(timer_) {};

    void forward(async<>&& ctx) override {
        // NOTE: Marking timer released.
        // NOTE: Because await_ready() will be called after context retreatment to runner.
        // NOTE: And retreatment will happen only when timer actually expired
        _timer->_released = true;
        core::clock::subscribe(std::move(ctx), _timer->_duration);
    }

    ~timer_conductor() override = default;

    timer* const _timer;
};


ACE_FUTURE_TIMER_MEMBER(bool)
await_suspend(auto coroutine) {
    coroutine.promise()._conductor = timer_conductor{this};
    return true;
}

#undef ACE_FUTURE_TIMER_MEMBER
#undef ACE_FUTURE_TIMER_SPACE
#endif // ACE_FUTURE_TIMER_H
