/**
 * @file timeout.h
 * @brief Timer futures: @c ace::futures::timeout and @c ace::futures::expire.
 *
 * @details Both types suspend the calling coroutine for a time interval and
 * resume it via the @c clock service.
 *
 * ### How it works
 *
 * 1. @c co_await timeout(dur) calls @c await_suspend().
 * 2. A @c timeout_router is placed in the promise's router slot.
 * 3. The runner sees the router and calls @c router.redirect(node).
 * 4. The router calls @c clock::subscribe(node, dur) which inserts the
 *    node into the time wheel.
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

    struct timeout_router;
    friend timeout_router;

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
        _duration = std::chrono::duration_cast<std::chrono::milliseconds>(t);
        // NOTE: Negative durations must not reach the time wheel — a negative slot
        // offset would make it index a slot out of range (std::out_of_range).
        if (_duration < services::duration_t::zero()) [[unlikely]]
            _duration = services::duration_t::zero();
    };

    /// @brief Default constructor — zero duration.
    timeout() = default;

    /**
     * @brief C++20 awaitable protocol — install the @c timeout_router.
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

    /// @brief Default constructor — zero duration.
    expire() = default;
};

} // end namespace ace::futures

// NOTE: The short aliases ace::timeout / ace::expire are only exposed when
// ace/ace.h (quick-start header) was included before this file — its ACE_H
// guard switches aliases on.
#ifdef ACE_H
namespace ace {
    /// @brief Short alias for @c ace::futures::timeout.
    using timeout = futures::timeout;
    /// @brief Short alias for @c ace::futures::expire.
    using expire = futures::expire;
}
#endif


//==============================- DEFINITIONS -==================================


#define ACE_FUTURE_TIMEOUT_SPACE \
ace::futures::timeout::

#define ACE_FUTURE_TIMEOUT_MEMBER(returnT) \
returnT ACE_FUTURE_TIMEOUT_SPACE

/**
 * @brief Router that registers a suspended task in the clock service.
 *
 * @details On @c redirect() the task is subscribed to the clock's time wheel
 * with the timeout's duration; on @c cancel() the pending timer is detached
 * and the node returns to its runner.
 */
struct ACE_FUTURE_TIMEOUT_SPACE timeout_router : runner_router {

    /// @brief Default construction is forbidden — a timeout owner is required.
    timeout_router() = delete;

    /**
     * @brief Binds the router to the owning timeout.
     * @param timeout_ Pointer to the owning timeout future.
     */
    explicit timeout_router(timeout* timeout_)
        : _timeout(timeout_) {};

    /**
     * @brief Subscribes the suspended task to the clock service.
     * @param node Task node to schedule for wake-up.
     */
    void redirect(const omni_node node) override {
        _injected_node = services::clock::subscribe(node, _timeout->_duration);
    }

    /**
     * @brief Cancels the pending timer and returns the node to its runner.
     */
    void cancel() override {
        if (_injected_node) {
            services::clock::detach(_injected_node);
            _injected_node = nullptr;
        }
    }

    ~timeout_router() override = default;

    services::timer_node* _injected_node = nullptr; ///< Timer node registered in the clock wheel.
    timeout* const _timeout;                        ///< Owning timeout future.
};


ACE_FUTURE_TIMEOUT_MEMBER(bool)
/**
 * @brief Installs the timeout router into the caller's promise.
 * @param coroutine Caller coroutine promise accessor.
 * @return Always @c true — the coroutine always suspends.
 */
await_suspend(auto coroutine) {
    coroutine.promise()._runner_router = timeout_router{this};
    return true;
}

#undef ACE_FUTURE_TIMEOUT_MEMBER
#undef ACE_FUTURE_TIMEOUT_SPACE
#endif // ACE_FUTURE_TIMEOUT_H
