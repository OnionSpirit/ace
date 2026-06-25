/**
 * @file timeout.h
 * @brief Timer futures: @c ace::futures::timeout and @c ace::futures::expire.
 *
 * @details Both types suspend the calling coroutine for a time interval and
 * resume it via the @c clock vortex service.
 *
 * ### How it works
 *
 * 1. @c co_await timeout(dur) calls @c await_suspend().
 * 2. A @c timeout_conductor is placed in the promise's conductor slot.
 * 3. The runner sees the conductor and calls @c conductor.forward(task).
 * 4. The conductor calls @c clock::subscribe(task, dur) which inserts the
 *    task into the time wheel.
 * 5. When @c dur elapses the clock's @c ping() releases the task back to its
 *    runner via @c runner::reattach().
 *
 * @c expire is a thin wrapper around @c timeout that accepts an @b absolute
 * @c timepoint_t instead of a relative duration.
 *
 * @par Example
 * @code{.cpp}
 * using namespace std::chrono_literals;
 *
 * ace::task timed() {
 *     co_await ace::futures::timeout(500ms);
 *
 *     auto deadline = ace::core::clock::current_time() + 2s;
 *     co_await ace::futures::expire(deadline);
 *     co_return;
 * }
 * @endcode
 */
#ifndef ACE_FUTURE_TIMEOUT_H
#define ACE_FUTURE_TIMEOUT_H

#include <ace/services/clock.h>
#include <ace/core/traits/future.h>
#include <ace/core/async.h>

using namespace std::chrono_literals;

namespace ace::futures {

/**
 * @brief Future that suspends the coroutine for a relative duration.
 *
 * @details The duration is converted to milliseconds at construction time.
 * Minimum resolution is 1 ms (limited by the clock tick duration).
 */
class ACE_AWAIT_NODISCARD timeout : public core::traits::future_traits<timeout> {

    services::duration_t _duration; ///< Suspension duration in milliseconds.

    struct timeout_conductor;
    friend timeout_conductor;

public:

    IMPORT_FUTURE_ENV(timeout)

    /**
     * @brief Construct a timeout future.
     * @tparam I  Integer representation type of the duration.
     * @tparam T  Period type of the duration.
     * @param t   Duration to wait.  Converted to @c std::chrono::milliseconds.
     */
    template <typename I, typename T>
    requires std::is_integral_v<I>
    explicit timeout(std::chrono::duration<I, T> t) {
        _duration = std::chrono::duration_cast<std::chrono::milliseconds, uint64_t, std::milli>(t);
    };

    timeout() = default;

    /**
     * @brief C++20 awaitable protocol — install the @c timeout_conductor.
     * @param coroutine  Handle to the suspending coroutine's promise.
     * @return Always @c true — the coroutine always suspends.
     */
    bool await_suspend(auto coroutine);

    void await_resume() {} ///< No value produced.
};

/**
 * @brief Future that suspends the coroutine until an absolute timepoint.
 *
 * @details Computed as @c expires - clock::current_time() and delegated to
 * @c timeout.
 *
 * @par Example
 * @code{.cpp}
 * auto deadline = ace::core::clock::current_time() + std::chrono::seconds(5);
 * co_await ace::futures::expire(deadline);
 * @endcode
 */
struct ACE_AWAIT_NODISCARD expire : timeout {
    /**
     * @brief Construct from an absolute timepoint.
     * @param expires  The absolute deadline.  The computed duration is
     *                 @c expires - clock::current_time().
     */
    explicit expire(services::timepoint_t expires)
        : timeout(expires - services::clock::current_time()) {}

    expire() = default;
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

    void forward(task&& ctx) override {
        _injected_node = services::clock::subscribe(std::move(ctx), _timeout->_duration);
    }

    void cancel() override {
        if (_injected_node)
            services::clock::detach(_injected_node);
    }

    ~timeout_conductor() override = default;

    services::clock_node* _injected_node = nullptr;
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
