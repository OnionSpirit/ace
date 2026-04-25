/**
 * @file dispatch.h
 * @brief C++20 concepts for ACE awaitable and future type classification.
 *
 * @details These concepts are used by @c promise_traits::await_transform() to
 * select the correct handling path for a @c co_await expression:
 *
 *  - <b>@c is_awaitable</b> — the minimal C++20 awaitable interface
 *    (@c await_ready, @c await_suspend, @c await_resume).
 *  - <b>@c is_future</b> — a type that additionally inherits from
 *    @c ace::futures::future_traits<Derived>.  The runner hands control
 *    entirely to the future via the conductor mechanism.
 *  - <b>@c is_busy_future</b> — a type that inherits from
 *    @c ace::futures::busy_future_traits<Derived>.  The runner actively polls
 *    @c await_ready() before re-queueing the task.
 *
 * @see ace::futures::future_traits, ace::futures::busy_future_traits,
 *      ace::coroutines::promise_traits
 */
#ifndef DISPATCH_H
#define DISPATCH_H

#include <concepts>
#include <coroutine>

/**
 * @brief Concepts used by @c promise_traits::await_transform() to classify
 *        types passed to @c co_await.
 */
namespace ace::misc::dispatch {

    /**
     * @brief Minimal C++20 awaitable interface.
     *
     * @details A type satisfies @c is_awaitable if it provides:
     *  - @c bool await_ready()
     *  - @c await_resume()
     *  - @c await_suspend(coroutine_handle<P>) returning @c bool, @c void, or
     *    @c coroutine_handle<P>.
     *
     * @tparam awaitableT  Type to check.
     * @tparam promiseT    Promise type of the enclosing coroutine.
     */
    template <typename awaitableT, typename promiseT>
    concept is_awaitable =
        requires (awaitableT awaitable_t, std::coroutine_handle<promiseT> promise_t) {
        { awaitable_t.await_ready() } -> std::same_as<bool>;
        awaitable_t.await_resume();
        // awaitable_t.detach(promise_t);
        }
    and (
         requires (awaitableT awaitable_t, std::coroutine_handle<promiseT> promise_t) {
             { awaitable_t.await_suspend(promise_t) } -> std::same_as<std::coroutine_handle<promiseT>>; }
         or requires (awaitableT awaitable_t, std::coroutine_handle<promiseT> promise_t) {
             { awaitable_t.await_suspend(promise_t) } -> std::same_as<bool>; }
         or requires (awaitableT awaitable_t, std::coroutine_handle<promiseT> promise_t) {
             { awaitable_t.await_suspend(promise_t) } -> std::same_as<void>; }
         );

    /**
     * @brief ACE future concept (conductor-based suspension).
     *
     * @details A type satisfies @c is_future if it:
     *  1. Exposes a nested @c future_traits_t alias.
     *  2. Is derived from @c future_traits_t (i.e., from
     *     @c ace::futures::future_traits<Derived>).
     *  3. Satisfies @c is_awaitable.
     *
     * When @c promise_traits::await_transform() detects this concept, it clears
     * @c _busy_future so the runner uses the conductor for forwarding.
     *
     * @tparam futureT   Type to check.
     * @tparam promiseT  Promise type of the enclosing coroutine.
     */
    template <typename futureT, typename promiseT>
    concept is_future =
        requires { typename futureT::future_traits_t; }
    and std::derived_from<futureT, typename futureT::future_traits_t>
    and is_awaitable<futureT, promiseT>;

    /**
     * @brief ACE busy-future concept (active polling suspension).
     *
     * @details A type satisfies @c is_busy_future if it:
     *  1. Exposes a nested @c busy_future_traits_t alias.
     *  2. Is derived from @c busy_future_traits_t (i.e., from
     *     @c ace::futures::busy_future_traits<Derived>).
     *  3. Satisfies @c is_awaitable.
     *
     * When @c promise_traits::await_transform() detects this concept, it stores
     * a pointer in @c _busy_future.  The runner calls @c await_ready() repeatedly
     * before deciding to re-queue the task, avoiding a full conductor round-trip
     * for fast operations (e.g., channel pull when data is already available).
     *
     * @tparam futureT   Type to check.
     * @tparam promiseT  Promise type of the enclosing coroutine.
     */
    template <typename futureT, typename promiseT>
    concept is_busy_future =
        requires { typename futureT::busy_future_traits_t; }
    and std::derived_from<futureT, typename futureT::busy_future_traits_t>
    and is_awaitable<futureT, promiseT>;

    /**
     * @brief ACE commonized future concept (active polling or conductor-based suspensions).
     *
     * @details Detects both @b active @b polling @c busy_future and @b conductor-based @c future
     *
     * @tparam futureT   Type to check.
     * @tparam promiseT  Promise type of the enclosing coroutine.
     */
    template <typename futureT, typename promiseT>
    concept is_any_future = is_busy_future<futureT, promiseT> or is_future<futureT, promiseT>;

}
#endif //DISPATCH_H
