/**
 * @file
 * @details This file contains a future_handler, future_trait classes and its
 * dispatching concepts: is_future_accept_promise, is_future_accept_coroutine,
 * is_future. Types are intended to be used to create derived future objects,
 * that will be processed by co_await operator, and make promises waits to its
 * result
 */

#ifndef ACE_FUTURE_H
#define ACE_FUTURE_H

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace ace::async {

    /**
     * @details Handler for future objects
     */
    struct future_handler {

        /**
         * @details Future value check function,
         * @b true if value is ready
         * @b flase otherwise
         * @remark Spicified by C++20 standart
         */
        virtual bool await_ready() = 0;

        /**
         * @details Default destructor
         */
        virtual ~future_handler() = default;
    };

    /**
     * @details Traits class future objects
     * @tparam derivedT Derived type
     */
    template <typename derivedT>
    struct future_traits : future_handler {

        using derived_future_t = derivedT;

        /**
         * @details Allows
         * use created object with @b co_await
         * @b operator inside context code,
         * and makes derived class @b awaitable
         */
        derived_future_t& operator co_await() requires (std::copy_constructible<derived_future_t>) {
            return *static_cast<derived_future_t*>(this);
        }

        /**
         * @details Allows
         * use created object with @b co_await
         * @b operator inside context code,
         * and makes derived class @b awaitable
         */
        derived_future_t&& operator co_await() requires (not std::copy_constructible<derived_future_t>) {
            static_assert(std::move_constructible<derived_future_t>, "At least move constructability required");
            return std::forward<derived_future_t>(*static_cast<derived_future_t*>(this));
        }

        /**
         * @details Default destructor
         */
        ~future_traits() override = default;
    };

    #define DECLARE_FUTURE(future_t) typedef future_traits<future_t> future_traits_t;

    #define IMPORT_FUTURE_ENV using typename future_traits_t::derived_future_t;


    /**
     * @brief namespace for dispatch concepts declaration
     */
    namespace dispatch {

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

    }

}

#endif // ACE_FUTURE_H
