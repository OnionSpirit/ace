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

#include <variant>
#include <tuple>
#include <concepts>
#include <memory>

namespace ace::core::traits {

    /**
     * @details Handler for future objects
     */
    struct future_handle {

        /**
         * @details Future value check function,
         * @b true if value is ready
         * @b false otherwise
         * @remark Specified by C++20 standard
         */
        virtual bool await_ready() = 0;

        /**
         * @details Default destructor
         */
        virtual ~future_handle() = default;
    };

    /**
     * @details Trait class for future objects
     * @tparam derivedT Derived type
     */
    template <typename derivedT>
    struct future_traits : future_handle {

        using derived_future_t = derivedT;

        /**
         * @details Allows
         * use created object with @b co_await
         * @b operator inside async code,
         * and makes derived class @b awaitable
         */
        auto&& operator co_await() {
            return std::move(*static_cast<derived_future_t*>(this));
        }

        bool await_ready() override { return false; };
    };

    #define IMPORT_FUTURE_ENV(future_t)                                      \
        typedef ace::core::traits::future_traits<future_t> future_traits_t;  \
        using typename future_traits_t::derived_future_t;


    /**
     * @details Traits class busy future objects. (Active polling of awaited tasks)
     * @tparam derivedT Derived type
     */
    template <typename derivedT>
    struct busy_future_traits : future_handle {

        using derived_busy_future_t = derivedT;

        /**
         * @details Allows
         * use created object with @b co_await
         * @b operator inside async code,
         * and makes derived class @b awaitable
         */
        derived_busy_future_t& operator co_await() requires (std::copy_constructible<derived_busy_future_t>) {
            return *static_cast<derived_busy_future_t*>(this);
        }

        /**
         * @details Allows
         * use created object with @b co_await
         * @b operator inside async code,
         * and makes derived class @b awaitable
         */
        derived_busy_future_t&& operator co_await() requires (not std::copy_constructible<derived_busy_future_t>) {
            // static_assert(std::move_constructible<derived_future_t>, "At least move constructability required");
            return std::forward<derived_busy_future_t>(*static_cast<derived_busy_future_t*>(this));
        }

        /**
         * @details Default destructor
         */
        ~busy_future_traits() override = default;
    };

    #define IMPORT_BUSY_FUTURE_ENV(future_t)                                           \
        typedef ace::core::traits::busy_future_traits<future_t> busy_future_traits_t;  \
        using typename busy_future_traits_t::derived_busy_future_t;

}

namespace ace::core::meta {

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
        requires (awaitableT awaitable_t) {
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
    concept is_future_accurate =
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
    concept is_busy_future_accurate =
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
    concept is_any_future_accurate = is_busy_future_accurate<futureT, promiseT> or is_future_accurate<futureT, promiseT>;

    /**
     * @brief ACE future concept (conductor-based suspension).
     *
     * @details A type satisfies @c is_future if it:
     *  1. Exposes a nested @c future_traits_t alias.
     *  2. Is derived from @c future_traits_t (i.e., from
     *     @c ace::futures::future_traits<Derived>).
     *
     * When @c promise_traits::await_transform() detects this concept, it clears
     * @c _busy_future so the runner uses the conductor for forwarding.
     *
     * @tparam futureT   Type to check.
     */
    template <typename futureT>
    concept is_future = (
        requires { typename futureT::future_traits_t; }
     or requires { typename futureT::busy_future_traits_t; }
    )
    and (
        std::derived_from<futureT, typename futureT::future_traits_t>
     or std::derived_from<futureT, typename futureT::busy_future_traits_t>
    )
    and requires (futureT awaitable_t) {
        { awaitable_t.await_ready() } -> std::same_as<bool>;
        awaitable_t.await_resume();
    };

    template <is_future future_t>
    using resume_type = decltype(std::declval<future_t>().await_resume());

    /**
     * @tparam inspect_t - Type to analyze
     * @tparam expected_t - Type that you are looking for
     * @tparam replace_with_t - Type to replace if @c inspect_t and @c current_t are same
     */
    template <typename inspect_t, typename expected_t = void, typename replace_with_t = std::monostate>
    using replace_type = std::conditional_t<std::same_as<inspect_t, expected_t>, replace_with_t, inspect_t>;

    namespace details {
        template <typename T, typename... Ts>
       struct unique_impl { using type = T; };

        template <template<class...> class Tuple, typename... Ts, typename U, typename... Us>
        struct unique_impl<Tuple<Ts...>, U, Us...>
            : std::conditional_t<(std::is_same_v<U, Ts> || ...),
                                 unique_impl<Tuple<Ts...>, Us...>,
                                 unique_impl<Tuple<Ts..., U>, Us...>> {};

        template <class Tuple>
        struct unique_tuple;

        template <template<class...> class Tuple, typename... Ts>
        struct unique_tuple<Tuple<Ts...>> : unique_impl<Tuple<>, Ts...> {};
    }

    template <class Tuple>
    using unique_tuple_t = details::unique_tuple<Tuple>::type;

    namespace details {
        // Helper to convert tuple to variant
        template <typename Tuple>
        struct tuple_to_variant;

        template <typename... Ts>
        struct tuple_to_variant<std::tuple<Ts...>> {
            using type = std::variant<Ts...>;
        };
    }

    // Convenient alias
    template <typename Tuple>
    using tuple_to_variant_t = details::tuple_to_variant<Tuple>::type;

    template <typename> struct is_tuple_t: std::false_type::type {};

    template <typename ...T> struct is_tuple_t<std::tuple<T...>>: std::true_type::type {};

    template <typename type>
    inline constexpr bool is_tuple_v = is_tuple_t<type>::value;

    template <std::size_t index, typename ... pack_ts>
    using at_pack = std::decay_t<decltype(std::get<index>(std::declval<std::tuple<pack_ts...>>()))>;

}

#endif // ACE_FUTURE_H
