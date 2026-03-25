#ifndef DISPATCH_H
#define DISPATCH_H

#include <concepts>
#include <coroutine>

/**
 * @brief namespace for dispatch concepts declaration
 */
namespace ace::common::dispatch {

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

    template <typename futureT, typename promiseT>
    concept is_future =
        requires { typename futureT::future_traits_t; }
    and std::derived_from<futureT, typename futureT::future_traits_t>
    and is_awaitable<futureT, promiseT>;

    template <typename futureT, typename promiseT>
    concept is_busy_future =
        requires { typename futureT::busy_future_traits_t; }
    and std::derived_from<futureT, typename futureT::busy_future_traits_t>
    and is_awaitable<futureT, promiseT>;


}
#endif //DISPATCH_H
