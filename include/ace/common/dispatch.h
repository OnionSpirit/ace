/**
 * @file dispatch.h
 * @brief C++20 concepts for ACE awaitable and future type classification.
 *
 * @details These concepts are used by `promise_traits::await_transform()` to
 * select the correct handling path for a `co_await` expression:
 *
 *  - **`is_awaitable`** — the minimal C++20 awaitable interface
 *    (`await_ready`, `await_suspend`, `await_resume`).
 *  - **`is_future`** — a type that additionally inherits from
 *    `ace::futures::future_traits<Derived>`.  The runner hands control
 *    entirely to the future via the conductor mechanism.
 *  - **`is_busy_future`** — a type that inherits from
 *    `ace::futures::busy_future_traits<Derived>`.  The runner actively polls
 *    `await_ready()` before re-queueing the task.
 *
 * @see ace::futures::future_traits, ace::futures::busy_future_traits,
 *      ace::coroutines::promise_traits
 */
#ifndef DISPATCH_H
#define DISPATCH_H

#include <concepts>
#include <coroutine>

/**
 * @brief Concepts used by `promise_traits::await_transform()` to classify
 *        types passed to `co_await`.
 */
namespace ace::common::dispatch {

    /**
     * @brief Minimal C++20 awaitable interface.
     *
     * @details A type satisfies `is_awaitable` if it provides:
     *  - `bool await_ready()`
     *  - `await_resume()`
     *  - `await_suspend(coroutine_handle<P>)` returning `bool`, `void`, or
     *    `coroutine_handle<P>`.
     *
     * @tparam awaitableT  Type to check.
     * @tparam promiseT    Promise type of the enclosing coroutine.
     */
    template <typename awaitableT, typename promiseT>
    concept is_awaitable =
        requires (awaitableT awaitable_t, std::coroutine_handle<promiseT> promise_t) {
            { awaitable_t.await_ready() } -> std::same_as<bool>;
            awaitable_t.await_resume();
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
     * @details A type satisfies `is_future` if it:
     *  1. Exposes a nested `future_traits_t` alias.
     *  2. Is derived from `future_traits_t` (i.e., from
     *     `ace::futures::future_traits<Derived>`).
     *  3. Satisfies `is_awaitable`.
     *
     * When `promise_traits::await_transform()` detects this concept, it clears
     * `_busy_future` so the runner uses the conductor for forwarding, avoiding busy-polling for tasks.
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
     * @details A type satisfies `is_busy_future` if it:
     *  1. Exposes a nested `busy_future_traits_t` alias.
     *  2. Is derived from `busy_future_traits_t` (i.e., from
     *     `ace::futures::busy_future_traits<Derived>`).
     *  3. Satisfies `is_awaitable`.
     *
     * When `promise_traits::await_transform()` detects this concept, it stores
     * a pointer in `_busy_future`.  The runner calls `await_ready()` repeatedly
     * before deciding to re-queue the task. This type of future does not prevent busy-polling
     *
     * @tparam futureT   Type to check.
     * @tparam promiseT  Promise type of the enclosing coroutine.
     */
    template <typename futureT, typename promiseT>
    concept is_busy_future =
        requires { typename futureT::busy_future_traits_t; }
    and std::derived_from<futureT, typename futureT::busy_future_traits_t>
    and is_awaitable<futureT, promiseT>;


}
#endif //DISPATCH_H
