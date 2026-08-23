/**
 * @file compose.h
 * @brief Future composition primitives — logical AND/OR combinators and monadic
 *        piping (@c operator>>) for ACE awaitables.
 *
 * @details This header provides the building blocks for composing multiple
 * asynchronous operations:
 *
 *  - <b>AND combinators</b> (@c and_await, @c and_await_composed) — await all
 *    operands and collect their results (as @c std::tuple or a single value).
 *  - <b>OR combinators</b> (@c or_await, @c or_await_composed) — await the
 *    first operand that finishes and cancel the rest.
 *  - <b>Monadic pipe</b> (@c compose(), @c operator>>) — forward the result of
 *    one future into a responder coroutine or function.
 *
 * ### AND vs OR
 *
 * | Combinator | Arity | Return on @c void | Return on typed |
 * |---|---|---|---|
 * | @c and_await | 2 | @c std::monostate | @c std::tuple<T1,T2> |
 * | @c and_await_composed | N | @c std::monostate | @c std::tuple<T1..TN> |
 * | @c or_await | 2 | @c int (winner index) | @c std::variant<T1,T2> |
 * | @c or_await_composed | N | @c int (winner index) | variant of unique types |
 *
 * ### How combinators work
 *
 * Each combinator creates observer tasks — one per operand future.  Observers
 * run on the same runner as the caller (via @c attach_front()).  When an
 * observer finishes:
 *
 *  - <b>OR</b>: the winner cancels all other observers, stores the result, and
 *    re-attaches the waiting caller.
 *  - <b>AND</b>: the last finishing observer re-attaches the waiting caller,
 *    after all others have completed.
 *
 * @see ace::core::or_await, ace::core::and_await, ace::core::compose
 */
#ifndef ACE_CORE_COMPOSE_H
#define ACE_CORE_COMPOSE_H

#include <optional>
#include <variant>

#include "ace/core/async.h"
#include "ace/core/dispatcher.h"
#include "ace/core/async_handle.h"
#include "ace/core/traits/future.h"

namespace ace::core {

    /**
     * @brief Awaitable that races two futures — the first to finish wins.
     *
     * @details When @c co_await-ed, spawns two observer tasks (one per operand)
     * on the current runner.  The first observer that completes stores its
     * result in @c _result, cancels the other observer, and re-attaches the
     * waiting caller.  The loser's observer is dropped.
     *
     * Return type depends on operand types:
     *  - Both @c void → @c int (index of the winner: 0 = left, 1 = right).
     *  - One @c void, one typed → @c std::optional<T> (non-void type).
     *  - Both typed → @c std::variant<LeftT, RightT>.
     *
     * @tparam l_future_t  Left operand future type.
     * @tparam r_future_t  Right operand future type.
     *
     * @see ace::core::or_await_composed (variadic version)
     */
    template <typename l_future_t, typename r_future_t>
    struct ACE_AWAIT_NODISCARD or_await final : traits::future_traits<or_await<l_future_t, r_future_t>> {

        IMPORT_FUTURE_ENV(or_await);

        struct or_await_router;
        friend or_await_router;

        typedef meta::resume_type<l_future_t> l_future_ret_t;
        typedef meta::resume_type<r_future_t> r_future_ret_t;

        /**
         * @brief Races two futures.
         * @param l_future Left operand future.
         * @param r_future Right operand future.
         */
        or_await(l_future_t& l_future, r_future_t& r_future)
            : _l_future(l_future)
            , _r_future(r_future) {};

        /**
         * @brief Compile-time deduction of the race result type.
         * @return A representative value whose type becomes @c return_t.
         */
        static consteval auto define_return_type() {
            if constexpr (std::same_as<void, l_future_ret_t> and std::same_as<void, r_future_ret_t>)
                return int();
            // Begin: syntax sugar
            else if constexpr (std::same_as<void, l_future_ret_t> and not std::same_as<void, r_future_ret_t>)
                return std::optional<r_future_ret_t>{};
            else if constexpr (std::same_as<void, r_future_ret_t> and not std::same_as<void, l_future_ret_t>)
                return std::optional<l_future_ret_t>{};
            // End: syntax sugar
            else return std::variant<l_future_ret_t, r_future_ret_t>{};
        }

        typedef decltype(define_return_type()) return_t;

        omni_node _waiter;                                            ///< Suspended caller awaiting the race result.
        l_future_t& _l_future;                                        ///< Left operand future.
        r_future_t& _r_future;                                        ///< Right operand future.
        std::optional<async_handle<>> _l_future_observer;             ///< Handle of the left observer task.
        std::optional<async_handle<>> _r_future_observer;             ///< Handle of the right observer task.
        return_t _result;                                             ///< Stored result of the winning future.

        /**
         * @brief Observer task that awaits one operand.
         * @details On completion stores the result (if typed), cancels the
         * opposite observer, and re-attaches the waiting caller.
         * @tparam observer_idx      Index of this observer (0 = left, 1 = right).
         * @tparam future_t          Operand future type.
         * @param future             Operand future to await.
         * @param opposite_observer  Handle of the other observer to cancel.
         */
        template <size_t observer_idx, typename future_t>
        task observer(future_t& future, std::optional<async_handle<>>& opposite_observer) {

            typedef decltype(std::declval<future_t>().await_resume()) future_ret_t;

            if constexpr (std::same_as<return_t, std::variant<l_future_ret_t, r_future_ret_t>>)
                _result.template emplace<observer_idx>(co_await future);
            else if constexpr (std::same_as<return_t, std::optional<future_ret_t>>)
                _result = co_await future;
            else
                co_await future;

            if (opposite_observer)
                opposite_observer->cancel();

            if (_waiter)
                runner::reattach(_waiter);

            // NOTE: Setting finished operand ID if both operands are void awaitable
            if constexpr (std::same_as<int, return_t>)
                _result = observer_idx;
        };

        /**
         * @brief Suspends the caller and starts both observers.
         * @details The coroutine promise accessor identifies the caller and its runner.
         * @return Always @c true — the caller is always suspended.
         */
        bool await_suspend(auto);

        /**
         * @brief Returns the race result.
         * @return @c int, @c std::optional<T> or @c std::variant — see class docs.
         */
        return_t await_resume() { return _result; };
    };

    /**
     * @brief Awaitable that waits for both futures to complete.
     *
     * @details When @c co_await-ed, spawns two observer tasks on the current
     * runner.  Both observers run to completion; the second one to finish
     * re-attaches the waiting caller.  Results from both operands are collected.
     *
     * Return type depends on operand types:
     *  - Both @c void → @c std::monostate (@c await_resume() returns @c void).
     *  - One @c void, one typed → the non-@c void type.
     *  - Both typed → @c std::tuple<LeftT, RightT>.
     *
     * @tparam l_future_t  Left operand future type.
     * @tparam r_future_t  Right operand future type.
     *
     * @see ace::core::and_await_composed (variadic version)
     */
    template <typename l_future_t, typename r_future_t>
    struct ACE_AWAIT_NODISCARD and_await final : traits::future_traits<and_await<l_future_t, r_future_t>> {

        IMPORT_FUTURE_ENV(and_await);

        struct and_await_router;
        friend and_await_router;

        typedef meta::resume_type<l_future_t> l_future_ret_t;
        typedef meta::resume_type<r_future_t> r_future_ret_t;

        /**
         * @brief Waits for both futures.
         * @param l_future Left operand future.
         * @param r_future Right operand future.
         */
        and_await(l_future_t& l_future, r_future_t& r_future)
            : _l_future(l_future)
            , _r_future(r_future) {};

        /**
         * @brief Compile-time deduction of the combined result type.
         * @return A representative value whose type becomes @c return_t.
         */
        static consteval auto define_return_type() {
            if constexpr (std::same_as<void, l_future_ret_t> and std::same_as<void, r_future_ret_t>)
                return std::monostate{}; /// 'await_resume()' will return void at this option
            // Begin: syntax sugar
            else if constexpr (std::same_as<void, l_future_ret_t> and not std::same_as<void, r_future_ret_t>)
                return r_future_ret_t{};
            else if constexpr (not std::same_as<void, l_future_ret_t> and std::same_as<void, r_future_ret_t>)
                return l_future_ret_t{};
            // End: syntax sugar
            else return std::tuple<l_future_ret_t, r_future_ret_t>{};
        }

        typedef decltype(define_return_type()) return_t;

        omni_node _waiter;                                            ///< Suspended caller awaiting both results.
        l_future_t& _l_future;                                        ///< Left operand future.
        r_future_t& _r_future;                                        ///< Right operand future.
        std::optional<async_handle<>> _l_future_observer;             ///< Handle of the left observer task.
        std::optional<async_handle<>> _r_future_observer;             ///< Handle of the right observer task.
        return_t _result;                                             ///< Stored combined result.

        /**
         * @brief Observer task that awaits one operand.
         * @details On completion stores its part of the result.  The second
         * observer joins the first and re-attaches the waiting caller.
         * @tparam observer_idx      Index of this observer (0 = left, 1 = right).
         * @tparam future_t          Operand future type.
         * @param future             Operand future to await.
         * @param opposite_observer  Handle of the other observer to join.
         */
        template <size_t observer_idx, typename future_t>
        task observer(future_t& future, std::optional<async_handle<>>& opposite_observer) {

            typedef meta::resume_type<future_t> future_ret_t;

            if constexpr (std::same_as<return_t, std::tuple<l_future_ret_t, r_future_ret_t>>)
                std::get<observer_idx>(_result) = co_await future;
            else if constexpr (std::same_as<return_t, future_ret_t>)
                _result = co_await future;
            else
                co_await future;

            // NOTE: Only second observer joins and reattaches
            if constexpr (observer_idx == 1)
                if (not opposite_observer.value().done())
                    co_await opposite_observer->join();

            if constexpr (observer_idx == 1)
                runner::reattach(_waiter);
        };

        /**
         * @brief Suspends the caller and starts both observers.
         * @details The coroutine promise accessor identifies the caller and its runner.
         * @return Always @c true — the caller is always suspended.
         */
        bool await_suspend(auto);

        /// @brief No-result completion for void operands.
        void await_resume() requires std::same_as<std::monostate, return_t> { }

        /**
         * @brief Returns the combined result.
         * @return The non-void operand type or @c std::tuple — see class docs.
         */
        return_t await_resume() requires (not std::same_as<std::monostate, return_t>) {
            return _result;
        };
    };

    /**
     * @brief Variadic OR combinator — races N futures, returns the first to finish.
     *
     * @details Generalization of @c or_await to an arbitrary number of operands.
     * Spawns one observer per future.  The winning observer cancels all others
     * and re-attaches the calling coroutine.  If all operands return @c void,
     * the result is an @c int (winner index); otherwise a @c std::variant of
     * unique non-void types.
     *
     * @tparam future_ts  Variadic pack of future types to race.
     *
     * @see ace::core::or_await (2-operand version)
     */
    template <meta::is_future ... future_ts>
    struct ACE_AWAIT_NODISCARD or_await_composed final : traits::future_traits<or_await_composed<future_ts...>> {

        IMPORT_FUTURE_ENV(or_await_composed);

        struct or_await_composed_router;
        friend or_await_composed_router;

        static constexpr int futures_amount = sizeof...(future_ts);
        static constexpr int top_observer_idx = futures_amount - 1;

        /**
         * @brief Races N futures.
         * @param futures Operand futures to race.
         */
        explicit or_await_composed(future_ts&... futures)
            : _futures(futures...) {};

        /**
         * @brief Compile-time deduction of the race result type.
         * @return A representative value whose type becomes @c return_t.
         */
        static consteval auto define_return_type() {
            typedef std::tuple<meta::replace_type<meta::resume_type<future_ts>, void, std::monostate>...> temp_ret_t;
            typedef meta::unique_tuple_t<temp_ret_t> ret_tuple_t;
            if constexpr (std::same_as<std::tuple<std::monostate>, ret_tuple_t>)
                return int();
            else
                return meta::tuple_to_variant_t<temp_ret_t>{};
        }

        typedef decltype(define_return_type()) return_t;

        omni_node _waiter;                                                          ///< Suspended caller awaiting the race result.
        std::tuple<future_ts&...> _futures;                                         ///< Tuple of operand futures.
        std::array<std::optional<async_handle<>>, sizeof...(future_ts)> _observers; ///< Handles of the observer tasks.
        return_t _result;                                                           ///< Stored result of the winning future.

        /**
         * @brief Observer task that awaits one operand.
         * @details On completion stores the result (if typed), cancels all other
         * observers, and re-attaches the waiting caller.
         * @tparam observer_idx  Index of this observer.
         * @tparam future_t      Operand future type.
         * @param future         Operand future to await.
         */
        template <size_t observer_idx, typename future_t>
        task observer(future_t& future) {

            typedef meta::resume_type<future_t> future_ret_t;

            if constexpr (not std::same_as<void, future_ret_t>)
                _result = co_await future;
            else
                co_await future;

            // NOTE: Only last observer joins and reattaches
            for (int i = 0; i < futures_amount; ++i) {
                if (i not_eq observer_idx and _observers[i])
                    _observers[i]->cancel();
            }

            if (_waiter.operator bool() and _waiter->_data.is_exist())
                runner::reattach(_waiter);

            // NOTE: Setting finished operand ID if both operands are void awaitable
            if constexpr (std::same_as<int, return_t>)
                _result = observer_idx;
        };

        /**
         * @brief Suspends the caller and starts all observers.
         * @details The coroutine promise accessor identifies the caller and its runner.
         * @return Always @c true — the caller is always suspended.
         */
        bool await_suspend(auto);

        /**
         * @brief Returns the race result.
         * @return @c int or @c std::variant — see class docs.
         */
        return_t await_resume() { return _result; };
    };

    /**
     * @brief Variadic AND combinator — awaits all N futures and collects results.
     *
     * @details Generalization of @c and_await to an arbitrary number of operands.
     * Spawns one observer per future.  The last observer to finish re-attaches
     * the calling coroutine.  If all operands return @c void, the result is
     * @c std::monostate; otherwise a @c std::tuple of return types.
     *
     * @tparam future_ts  Variadic pack of future types to await.
     *
     * @see ace::core::and_await (2-operand version)
     */
    template <meta::is_future ... future_ts>
    struct ACE_AWAIT_NODISCARD and_await_composed final : traits::future_traits<and_await_composed<future_ts...>> {

        IMPORT_FUTURE_ENV(and_await_composed);

        struct and_await_composed_router;
        friend and_await_composed_router;

        static constexpr int futures_amount = sizeof...(future_ts);
        static constexpr int top_observer_idx = futures_amount - 1;

        /**
         * @brief Awaits all N futures.
         * @param futures Operand futures to await.
         */
        explicit and_await_composed(future_ts&... futures)
            : _futures(futures...) {};

        /**
         * @brief Compile-time deduction of the combined result type.
         * @return A representative value whose type becomes @c return_t.
         */
        static consteval auto define_return_type() {
            typedef std::tuple<meta::replace_type<meta::resume_type<future_ts>>...> temp_ret_t;
            typedef meta::unique_tuple_t<temp_ret_t> ret_tuple_t;
            if constexpr (std::same_as<std::tuple<std::monostate>, ret_tuple_t>)
                return std::monostate{};
            else
                return temp_ret_t{};
        }

        typedef decltype(define_return_type()) return_t;

        omni_node _waiter;                                                          ///< Suspended caller awaiting all results.
        std::tuple<future_ts&...> _futures;                                         ///< Tuple of operand futures.
        std::array<std::optional<async_handle<>>, sizeof...(future_ts)> _observers; ///< Handles of the observer tasks.
        return_t _result;                                                           ///< Stored combined result.

        /**
         * @brief Observer task that awaits one operand.
         * @details On completion stores its part of the result.  The last
         * observer joins all others and re-attaches the waiting caller.
         * @tparam observer_idx  Index of this observer.
         * @tparam future_t      Operand future type.
         * @param future         Operand future to await.
         */
        template <size_t observer_idx, typename future_t>
        task observer(future_t& future) {

            typedef meta::resume_type<future_t> future_ret_t;

            if constexpr (not std::same_as<void, future_ret_t>)
                std::get<observer_idx>(_result) = co_await future;
            else
                co_await future;

            // NOTE: Only last observer joins and reattaches
            if constexpr (observer_idx == top_observer_idx) {
                for (auto& opposite_observer : _observers | std::views::take(top_observer_idx) ) {
                    if (not opposite_observer.value().done())
                        co_await opposite_observer->join();
                }
            }

            if constexpr (observer_idx == top_observer_idx)
                runner::reattach(_waiter);
        };

        /**
         * @brief Suspends the caller and starts all observers.
         * @details The coroutine promise accessor identifies the caller and its runner.
         * @return Always @c true — the caller is always suspended.
         */
        bool await_suspend(auto);

        /**
         * @brief Returns the combined result.
         * @return @c void, a single type or @c std::tuple — see class docs.
         */
        auto await_resume() {
            if constexpr (std::same_as<return_t, std::monostate>) return;
            else return _result;
        };

    };


// =============================================- OUTPUT COMPOSERS -====================================================

    /**
     * @brief Monadic composition — pipe a future's result into a responder coroutine.
     *
     * @details Awaits @c sender, then passes its return value to @c responder
     * and awaits the responder.  This is the implementation behind @c operator>>.
     *
     * Overloads handle both @c void and non-@c void sender return types, and
     * both coroutine and regular function responders.
     *
     * @tparam sender_t              The upstream future to await first.
     * @tparam async_return          Return type of the responder coroutine.
     * @tparam async_input           Input type of the responder (must match sender's resume type).
     * @tparam async_promise_rule_t  Suspension policy of the responder (default: @c differed).
     * @param sender     The upstream future.
     * The responder function type identifies the coroutine that receives the sender's result.
     * @return An @c ace::promise<async_return> that represents the composed operation.
     */
    template <
        meta::is_future sender_t,
        typename async_return, typename async_input,
        template <typename> typename async_promise_rule_t = lazy_rule
    > requires (not std::same_as<meta::resume_type<sender_t>, void>)
    //
    promise<async_return>
    compose(sender_t&& sender, async<async_return, async_promise_rule_t>(responder)(async_input)) {
        typedef meta::resume_type<sender_t> sender_resume_t;
        static_assert(std::same_as<std::decay_t<async_input>, sender_resume_t>, ACE_INCOMPATIBLE_COMPOSE_ERROR);
        co_return co_await responder(std::forward<sender_resume_t>(co_await (sender)));
    }


    template <
        meta::is_future sender_t,
        typename async_return,
        template <typename> typename async_promise_rule_t = lazy_rule
    > requires std::same_as<meta::resume_type<sender_t>, void>
    /**
     * @brief Compose a @c void sender with an argument-less responder coroutine.
     * @param sender     The upstream future to await first.
     * @param responder  The responder coroutine (no arguments).
     * @return An @c ace::promise<async_return> that represents the composed operation.
     */
    promise<async_return>
    compose(sender_t&& sender, async<async_return, async_promise_rule_t>(responder)()) {
        co_await sender;
        co_return co_await responder();
    }


    template <
        meta::is_future sender_t,
        typename foo_return, typename foo_input
    > requires (not std::same_as<meta::resume_type<sender_t>, void>)
    /**
     * @brief Compose a valued sender with a responder function taking one argument.
     * @param sender     The upstream future to await first.
     * @param responder  The responder function (takes sender's result as argument).
     * @return An @c ace::promise<foo_return> that represents the composed operation.
     */
    promise<foo_return>
    compose(sender_t&& sender, foo_return(responder)(foo_input)) {
        typedef meta::resume_type<sender_t> sender_resume_t;
        static_assert(std::same_as<std::decay_t<foo_input>, sender_resume_t>, ACE_INCOMPATIBLE_COMPOSE_ERROR);
        co_return responder(std::forward<sender_resume_t>(co_await (sender)));
    }


    template <
        meta::is_future sender_t,
        typename foo_return
    > requires std::same_as<meta::resume_type<sender_t>, void>
    /**
     * @brief Compose a @c void sender with an argument-less responder function.
     * @param sender     The upstream future to await first.
     * @param responder  The responder function (no arguments).
     * @return An @c ace::promise<foo_return> that represents the composed operation.
     */
    promise<foo_return>
    compose(sender_t&& sender, foo_return(responder)()) {
        co_await sender;
        co_return responder();
    }


    template <
        meta::is_future sender_t,
        typename callable_t
    > requires (not std::same_as<meta::resume_type<sender_t>, void>)
    /**
     * @brief Compose a valued sender with a generic callable (lambda, functor).
     * @param sender     The upstream future to await first.
     * @param responder  The callable (takes sender's result as argument).
     * @return An @c ace::promise of the callable's result type.
     */
    auto compose(sender_t&& sender, callable_t&& responder)
        -> promise<std::invoke_result_t<std::decay_t<callable_t>, meta::resume_type<sender_t>&&>>
    {
        typedef meta::resume_type<sender_t> sender_resume_t;
        using return_t = std::invoke_result_t<std::decay_t<callable_t>, sender_resume_t&&>;
        if constexpr (std::is_void_v<return_t>) {
            std::forward<callable_t>(responder)(std::forward<sender_resume_t>(co_await sender));
            co_return;
        } else {
            co_return std::forward<callable_t>(responder)(std::forward<sender_resume_t>(co_await sender));
        }
    }


    template <
        meta::is_future sender_t,
        typename callable_t
    > requires std::same_as<meta::resume_type<sender_t>, void>
    /**
     * @brief Compose a @c void sender with a generic callable (lambda, functor).
     * @param sender     The upstream future to await first.
     * @param responder  The callable (no arguments).
     * @return An @c ace::promise of the callable's result type.
     */
    auto compose(sender_t&& sender, callable_t&& responder)
        -> promise<std::invoke_result_t<std::decay_t<callable_t>>>
    {
        co_await sender;
        using return_t = std::invoke_result_t<std::decay_t<callable_t>>;
        if constexpr (std::is_void_v<return_t>) {
            std::forward<callable_t>(responder)();
            co_return;
        } else {
            co_return std::forward<callable_t>(responder)();
        }
    }

} // end namespace ace::core

//==============================- DEFINITIONS -==================================

#define ACE_COMPOSE_AWAIT_FUTURE_META \
    template <typename l_future_t, typename r_future_t>

#define ACE_OR_AWAIT_FUTURE_SPACE \
    ace::core::or_await<l_future_t, r_future_t>::

#define ACE_OR_AWAIT_FUTURE_MEMBER(return_t) \
    ACE_COMPOSE_AWAIT_FUTURE_META            \
    return_t ACE_OR_AWAIT_FUTURE_SPACE

#define ACE_AND_AWAIT_FUTURE_SPACE \
    ace::core::and_await<l_future_t, r_future_t>::

#define ACE_AND_AWAIT_FUTURE_MEMBER(return_t) \
    ACE_COMPOSE_AWAIT_FUTURE_META             \
    return_t ACE_AND_AWAIT_FUTURE_SPACE

#define ACE_COMPOSE_AWAIT_COMPOSED_FUTURE_META \
    template <ace::core::meta::is_future ... future_ts>

#define ACE_AND_AWAIT_COMPOSED_FUTURE_SPACE \
    ace::core::and_await_composed<future_ts...>::

#define ACE_AND_AWAIT_COMPOSED_FUTURE_MEMBER(return_t) \
    ACE_COMPOSE_AWAIT_COMPOSED_FUTURE_META             \
    return_t ACE_AND_AWAIT_COMPOSED_FUTURE_SPACE

#define ACE_OR_AWAIT_COMPOSED_FUTURE_SPACE \
    ace::core::or_await_composed<future_ts...>::

#define ACE_OR_AWAIT_COMPOSED_FUTURE_MEMBER(return_t)  \
    ACE_COMPOSE_AWAIT_COMPOSED_FUTURE_META             \
    return_t ACE_OR_AWAIT_COMPOSED_FUTURE_SPACE


ACE_COMPOSE_AWAIT_FUTURE_META
/**
 * @brief Router for @c or_await — stores the waiting task until the race resolves.
 *
 * @details When @c redirect() is called by the runner, the caller is saved in
 * @c _or_await->_waiter.  When an observer finishes, it calls
 * @c runner::reattach() on the stored waiter.  @c cancel() propagates
 * cancellation to both observer tasks.
 */
struct ACE_OR_AWAIT_FUTURE_SPACE or_await_router final : runner_router {

    or_await_router() = delete;

    /**
     * @brief Binds the router to the owning @c or_await.
     * @param or_await_ Pointer to the owning race combinator.
     */
    explicit or_await_router(or_await* or_await_)
        : _or_await(or_await_) {};

    /**
     * @brief Stores the suspended caller until the race resolves.
     * @param node Task node of the suspended caller.
     */
    void redirect(omni_node node) override {
        _or_await->_waiter = node;
    }

    /**
     * @brief Cancels both observers and returns the stored waiter to its runner.
     */
    void cancel() override {
        _or_await->_l_future_observer->cancel();
        _or_await->_r_future_observer->cancel();
        // NOTE: The stored waiter node must be returned to its runner,
        // otherwise the cancelled coroutine frame and its node leak.
        if (_or_await->_waiter.operator bool() and _or_await->_waiter->_data.is_exist())
            runner::reattach(_or_await->_waiter);
    }

    ~or_await_router() override = default;

    or_await* _or_await; ///< Owning race combinator.
};


/*
 * @brief Creates and posts both observer tasks, then registers the race router.
 * @param external_coro Caller coroutine promise accessor.
 * @return Always @c true — the caller is always suspended.
 */
ACE_OR_AWAIT_FUTURE_MEMBER(bool)
await_suspend(auto external_coro) {
    auto* runner_ptr = external_coro.promise()._runner.template as<runner>();
    // NOTE: Creating observers for each futures
    task _l_observer = observer<0>(_l_future, _r_future_observer);
    task _r_observer = observer<1>(_r_future, _l_future_observer);
    // NOTE: Creating Handlers for observation tasks
    _l_future_observer = async_handle {_l_observer.observe()};
    _r_future_observer = async_handle {_r_observer.observe()};
    // NOTE: Posting observers
    _l_observer._coroutine.promise()._roaming = external_coro.promise()._roaming = false;
    runner_ptr->attach_front(std::forward<task>(_l_observer));
    _r_observer._coroutine.promise()._roaming = external_coro.promise()._roaming = false;
    runner_ptr->attach_front(std::forward<task>(_r_observer));
    // NOTE: Setting router for external waiter
    external_coro.promise()._runner_router = or_await_router {this};
    return true;
}


ACE_COMPOSE_AWAIT_FUTURE_META
/**
 * @brief Router for @c and_await — stores the waiting task until both futures finish.
 *
 * @details Similar to @c or_await_router but the stored waiter is only
 * re-attached after both observer tasks have completed.
 */
struct ACE_AND_AWAIT_FUTURE_SPACE and_await_router final : runner_router {

    and_await_router() = delete;

    /**
     * @brief Binds the router to the owning @c and_await.
     * @param and_await_ Pointer to the owning AND combinator.
     */
    explicit and_await_router(and_await* and_await_)
        : _and_await(and_await_) {};

    /**
     * @brief Stores the suspended caller until both futures finish.
     * @param node Task node of the suspended caller.
     */
    void redirect(omni_node node) override {
        _and_await->_waiter = node;
    }

    /**
     * @brief Cancels both observers and returns the stored waiter to its runner.
     */
    void cancel() override {
        _and_await->_l_future_observer->cancel();
        _and_await->_r_future_observer->cancel();
        // NOTE: The stored waiter node must be returned to its runner,
        // otherwise the cancelled coroutine frame and its node leak.
        if (_and_await->_waiter.operator bool() and _and_await->_waiter->_data.is_exist())
            runner::reattach(_and_await->_waiter);
    }

    ~and_await_router() override = default;

    and_await* _and_await; ///< Owning AND combinator.
};


/*
 * @brief Creates and posts both observer tasks, then registers the AND router.
 * @param external_coro Caller coroutine promise accessor.
 * @return Always @c true — the caller is always suspended.
 */
ACE_AND_AWAIT_FUTURE_MEMBER(bool)
await_suspend(auto external_coro) {
    auto* runner_ptr = external_coro.promise()._runner.template as<runner>();
    // NOTE: Creating observers for each futures
    task _l_observer = observer<0>(_l_future, _r_future_observer);
    task _r_observer = observer<1>(_r_future, _l_future_observer);
    // NOTE: Creating Handlers for observation tasks
    _l_future_observer = async_handle {_l_observer.observe()};
    _r_future_observer = async_handle {_r_observer.observe()};
    // NOTE: Posting observers
    _l_observer._coroutine.promise()._roaming = external_coro.promise()._roaming = false;
    runner_ptr->attach_front(std::forward<task>(_l_observer));
    _r_observer._coroutine.promise()._roaming = external_coro.promise()._roaming = false;
    runner_ptr->attach_front(std::forward<task>(_r_observer));
    // NOTE: Setting router for external waiter
    external_coro.promise()._runner_router = and_await_router {this};
    return true;
}

ACE_COMPOSE_AWAIT_COMPOSED_FUTURE_META
/**
 * @brief Router for variadic @c and_await_composed.
 * @see and_await_router
 */
struct ACE_AND_AWAIT_COMPOSED_FUTURE_SPACE and_await_composed_router final : runner_router {

    and_await_composed_router() = delete;

    /**
     * @brief Binds the router to the owning @c and_await_composed.
     * @param and_await_composed_ Pointer to the owning variadic AND combinator.
     */
    explicit and_await_composed_router(and_await_composed* and_await_composed_)
        : _and_await_composed(and_await_composed_) {};

    /**
     * @brief Stores the suspended caller until all futures finish.
     * @param node Task node of the suspended caller.
     */
    void redirect(omni_node node) override {
        _and_await_composed->_waiter = node;
    }

    /**
     * @brief Cancels all observers and returns the stored waiter to its runner.
     */
    void cancel() override {
        for (auto& opposite_observer : _and_await_composed->_observers) {
            opposite_observer->cancel();
        }
        // NOTE: The stored waiter node must be returned to its runner,
        // otherwise the cancelled coroutine frame and its node leak.
        if (_and_await_composed->_waiter.operator bool() and _and_await_composed->_waiter->_data.is_exist())
            runner::reattach(_and_await_composed->_waiter);
    }

    ~and_await_composed_router() override = default;

    and_await_composed* _and_await_composed; ///< Owning variadic AND combinator.
};


/*
 * @brief Creates and posts all observer tasks, then registers the variadic AND router.
 * @param external_coro Caller coroutine promise accessor.
 * @return Always @c true — the caller is always suspended.
 */
ACE_AND_AWAIT_COMPOSED_FUTURE_MEMBER(bool)
await_suspend(auto external_coro) {
    auto* runner_ptr = external_coro.promise()._runner.template as<runner>();
    // NOTE: Creating observers for each futures
    [&] <std::size_t ... index> (std::index_sequence<index...>) {
        (...,[&]{
            task observer_inst = observer<index>(std::get<index>(_futures));
            // NOTE: Creating Handlers for observation tasks
            _observers[index] = async_handle {observer_inst.observe()};
            // NOTE: Posting observer
            observer_inst._coroutine.promise()._roaming = external_coro.promise()._roaming = false;
            runner_ptr->attach_front(std::forward<task>(observer_inst));
        }());
    }(std::make_index_sequence<sizeof...(future_ts)>{});
    // NOTE: Setting router for external waiter
    external_coro.promise()._runner_router = and_await_composed_router{this};
    return true;
}

ACE_COMPOSE_AWAIT_COMPOSED_FUTURE_META
/**
 * @brief Router for variadic @c or_await_composed.
 * @see or_await_router
 */
struct ACE_OR_AWAIT_COMPOSED_FUTURE_SPACE or_await_composed_router final : runner_router {

    or_await_composed_router() = delete;

    /**
     * @brief Binds the router to the owning @c or_await_composed.
     * @param or_await_composed_ Pointer to the owning variadic OR combinator.
     */
    explicit or_await_composed_router(or_await_composed* or_await_composed_)
        : _or_await_composed(or_await_composed_) {};

    /**
     * @brief Stores the suspended caller until the race resolves.
     * @param node Task node of the suspended caller.
     */
    void redirect(omni_node node) override {
        _or_await_composed->_waiter = node;
    }

    /**
     * @brief Cancels all observers and returns the stored waiter to its runner.
     */
    void cancel() override {
        for (auto& opposite_observer : _or_await_composed->_observers) {
            opposite_observer->cancel();
        }
        // NOTE: The stored waiter node must be returned to its runner,
        // otherwise the cancelled coroutine frame and its node leak.
        if (_or_await_composed->_waiter.operator bool() and _or_await_composed->_waiter->_data.is_exist())
            runner::reattach(_or_await_composed->_waiter);
    }

    ~or_await_composed_router() override = default;

    or_await_composed* _or_await_composed; ///< Owning variadic OR combinator.
};


/*
 * @brief Creates and posts all observer tasks, then registers the variadic OR router.
 * @param external_coro Caller coroutine promise accessor.
 * @return Always @c true — the caller is always suspended.
 */
ACE_OR_AWAIT_COMPOSED_FUTURE_MEMBER(bool)
await_suspend(auto external_coro) {
    auto* runner_ptr = external_coro.promise()._runner.template as<runner>();
    // NOTE: Creating observers for each futures
    [&] <std::size_t ... index> (std::index_sequence<index...>) {
        (...,[&]{
            task observer_inst = observer<index>(std::get<index>(_futures));
            // NOTE: Creating Handlers for observation tasks
            _observers[index] = async_handle {observer_inst.observe()};
            // NOTE: Posting observer
            observer_inst._coroutine.promise()._roaming = external_coro.promise()._roaming = false;
            runner_ptr->attach_front(std::forward<task>(observer_inst));
        }());
    }(std::make_index_sequence<sizeof...(future_ts)>{});
    // NOTE: Setting router for external waiter
    external_coro.promise()._runner_router = or_await_composed_router{this};
    return true;
}

#undef ACE_COMPOSE_AWAIT_FUTURE_META
#undef ACE_AND_AWAIT_FUTURE_MEMBER
#undef ACE_AND_AWAIT_FUTURE_SPACE
#undef ACE_OR_AWAIT_FUTURE_MEMBER
#undef ACE_OR_AWAIT_FUTURE_SPACE
#undef ACE_COMPOSE_AWAIT_COMPOSED_FUTURE_META
#undef ACE_AND_AWAIT_COMPOSED_FUTURE_MEMBER
#undef ACE_AND_AWAIT_COMPOSED_FUTURE_SPACE
#undef ACE_OR_AWAIT_COMPOSED_FUTURE_MEMBER
#undef ACE_OR_AWAIT_COMPOSED_FUTURE_SPACE

//==============================- OPERATOR DEFINITIONS -=========================

/**
 * @name Future composition operators
 *
 * @details Two families of operators enable compact expression of AND/OR
 * composition on awaitable futures:
 *
 *  - @c operator or — race two futures; returns @c or_await.
 *  - @c operator and — wait for both futures; returns @c and_await.
 *
 * When chained, the operators build @c or_await_composed /
 * @c and_await_composed (variadic versions).  Both lvalue and rvalue
 * combinations are supported.
 *
 * @note These are free-function operators found via ADL on the operand types.
 */
///@{

#define ACE_COMPOSE_MEMBERS_ASSERT                                                                                 \
    using namespace ace::core::meta;                                                                                 \
    static_assert (is_future<l_future_t>, "Left operand shall be future, and await interfaces shall be accessed");   \
    static_assert (is_future<r_future_t>, "Right operand shall be future, and await interfaces shall be accessed");  \
    if constexpr (is_future<l_future_t> and is_future<r_future_t>)                                                   \

template <typename l_future_t, typename r_future_t> auto
operator or(l_future_t&& l_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::or_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t> auto
operator or(l_future_t& l_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::or_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t> auto
operator or(l_future_t&& l_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::or_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t> auto
operator or(l_future_t& l_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::or_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t> auto
operator and(l_future_t&& l_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::and_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t> auto
operator and(l_future_t& l_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::and_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t> auto
operator and(l_future_t&& l_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::and_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t> auto
operator and(l_future_t& l_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::and_await{l_future, r_future};
}

#undef ACE_COMPOSE_MEMBERS_ASSERT

#define ACE_COMPOSE_MEMBERS_ASSERT                                                                                   \
    using namespace ace::core::meta;                                                                                 \
    static_assert (is_future<r_future_t>, "Right operand shall be future, and await interfaces shall be accessed");  \
    if constexpr (is_future<r_future_t>)                                                                             \

template <typename composed_l_future_t, typename composed_r_future_t, typename r_future_t> auto
operator and(ace::core::and_await<composed_l_future_t, composed_r_future_t>&& composed_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::and_await_composed{composed_future._l_future, composed_future._r_future, r_future};
}

template <typename composed_l_future_t, typename composed_r_future_t, typename r_future_t> auto
operator and(ace::core::and_await<composed_l_future_t, composed_r_future_t>&& composed_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::and_await_composed{composed_future._l_future, composed_future._r_future, r_future};
}

template <typename composed_l_future_t, typename composed_r_future_t, typename r_future_t> auto
operator and(ace::core::and_await<composed_l_future_t, composed_r_future_t>& composed_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::and_await_composed{composed_future._l_future, composed_future._r_future, r_future};
}

template <typename composed_l_future_t, typename composed_r_future_t, typename r_future_t> auto
operator and(ace::core::and_await<composed_l_future_t, composed_r_future_t>& composed_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::and_await_composed{composed_future._l_future, composed_future._r_future, r_future};
}

template <typename ... composed_future_ts, typename r_future_t> auto
operator and(ace::core::and_await_composed<composed_future_ts...>&& composed_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return std::make_from_tuple<ace::core::and_await_composed<composed_future_ts..., r_future_t>>(
        std::tuple_cat(composed_future._futures, std::tie(r_future))
    );
}

template <typename ... composed_future_ts, typename r_future_t> auto
operator and(ace::core::and_await_composed<composed_future_ts...>&& composed_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return std::make_from_tuple<ace::core::and_await_composed<composed_future_ts..., r_future_t>>(
        std::tuple_cat(composed_future._futures, std::tie(r_future))
    );
}

template <typename ... composed_future_ts, typename r_future_t> auto
operator and(ace::core::and_await_composed<composed_future_ts...>& composed_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return std::make_from_tuple<ace::core::and_await_composed<composed_future_ts..., r_future_t>>(
        std::tuple_cat(composed_future._futures, std::tie(r_future))
    );
}

template <typename ... composed_future_ts, typename r_future_t> auto
operator and(ace::core::and_await_composed<composed_future_ts...>& composed_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return std::make_from_tuple<ace::core::and_await_composed<composed_future_ts..., r_future_t>>(
        std::tuple_cat(composed_future._futures, std::tie(r_future))
    );
}

template <typename composed_l_future_t, typename composed_r_future_t, typename r_future_t> auto
operator or(ace::core::or_await<composed_l_future_t, composed_r_future_t>&& composed_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::or_await_composed{composed_future._l_future, composed_future._r_future, r_future};
}

template <typename composed_l_future_t, typename composed_r_future_t, typename r_future_t> auto
operator or(ace::core::or_await<composed_l_future_t, composed_r_future_t>&& composed_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::or_await_composed{composed_future._l_future, composed_future._r_future, r_future};
}

template <typename composed_l_future_t, typename composed_r_future_t, typename r_future_t> auto
operator or(ace::core::or_await<composed_l_future_t, composed_r_future_t>& composed_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::or_await_composed{composed_future._l_future, composed_future._r_future, r_future};
}

template <typename composed_l_future_t, typename composed_r_future_t, typename r_future_t> auto
operator or(ace::core::or_await<composed_l_future_t, composed_r_future_t>& composed_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return ace::core::or_await_composed{composed_future._l_future, composed_future._r_future, r_future};
}

template <typename ... composed_future_ts, typename r_future_t> auto
operator or(ace::core::or_await_composed<composed_future_ts...>&& composed_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return std::make_from_tuple<ace::core::or_await_composed<composed_future_ts..., r_future_t>>(
        std::tuple_cat(composed_future._futures, std::tie(r_future))
    );
}

template <typename ... composed_future_ts, typename r_future_t> auto
operator or(ace::core::or_await_composed<composed_future_ts...>&& composed_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return std::make_from_tuple<ace::core::or_await_composed<composed_future_ts..., r_future_t>>(
        std::tuple_cat(composed_future._futures, std::tie(r_future))
    );
}

template <typename ... composed_future_ts, typename r_future_t> auto
operator or(ace::core::or_await_composed<composed_future_ts...>& composed_future, r_future_t&& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return std::make_from_tuple<ace::core::or_await_composed<composed_future_ts..., r_future_t>>(
        std::tuple_cat(composed_future._futures, std::tie(r_future))
    );
}

template <typename ... composed_future_ts, typename r_future_t> auto
operator or(ace::core::or_await_composed<composed_future_ts...>& composed_future, r_future_t& r_future) {
    ACE_COMPOSE_MEMBERS_ASSERT
    return std::make_from_tuple<ace::core::or_await_composed<composed_future_ts..., r_future_t>>(
        std::tuple_cat(composed_future._futures, std::tie(r_future))
    );
}

///@}

// ========================================- OUTPUT COMPOSE CREATORS -==================================================

/**
 * @name Monadic pipe operator (@c operator>>)
 *
 * @details @c sender >> responder is syntactic sugar for @c compose(sender, responder).
 * It forwards the return value of @c sender as the argument to @c responder,
 * enabling a pipeline style:
 *
 * @code{.cpp}
 * co_await (open_file("data.txt") >> process_data);
 * @endcode
 *
 * Overloads handle both coroutine and regular function responders, with or
 * without input arguments.
 */
///@{
template <
    ace::core::meta::is_future sender_t,
    typename async_return, typename async_input,
    template <typename> typename async_promise_rule_t = ace::core::lazy_rule
> requires (not std::same_as<ace::core::meta::resume_type<sender_t>, void>)
//
ace::promise<async_return>
operator >> (sender_t&& sender, ace::core::async<async_return, async_promise_rule_t>(responder)(async_input)) {
    return compose(std::forward<sender_t>(sender), responder);
}


template <
    ace::core::meta::is_future sender_t,
    typename async_return,
    template <typename> typename async_promise_rule_t = ace::core::lazy_rule
> requires std::same_as<ace::core::meta::resume_type<sender_t>, void>
//
ace::promise<async_return>
operator >> (sender_t&& sender, ace::core::async<async_return, async_promise_rule_t>(responder)()) {
    return compose(std::forward<sender_t>(sender), responder);
}


template <
    ace::core::meta::is_future sender_t,
    typename foo_return, typename foo_input
> requires (not std::same_as<ace::core::meta::resume_type<sender_t>, void>)
//
ace::promise<foo_return>
operator >> (sender_t&& sender, foo_return(responder)(foo_input)) {
    return compose(std::forward<sender_t>(sender), responder);
}


template <
    ace::core::meta::is_future sender_t,
    typename foo_return
> requires std::same_as<ace::core::meta::resume_type<sender_t>, void>
//
ace::promise<foo_return>
operator >> (sender_t&& sender, foo_return(responder)()) {
    return compose(std::forward<sender_t>(sender), responder);
}


template <
    ace::core::meta::is_future sender_t,
    typename callable_t
> requires (
    not std::same_as<ace::core::meta::resume_type<sender_t>, void>
    and not std::is_function_v<std::remove_pointer_t<std::decay_t<callable_t>>>
)
//
auto operator >> (sender_t&& sender, callable_t&& responder) {
    return compose(std::forward<sender_t>(sender), std::forward<callable_t>(responder));
}


template <
    ace::core::meta::is_future sender_t,
    typename callable_t
> requires (
    std::same_as<ace::core::meta::resume_type<sender_t>, void>
    and not std::is_function_v<std::remove_pointer_t<std::decay_t<callable_t>>>
)
//
auto operator >> (sender_t&& sender, callable_t&& responder) {
    return compose(std::forward<sender_t>(sender), std::forward<callable_t>(responder));
}

#undef ACE_COMPOSE_MEMBERS_ASSERT
///@}

#endif //ACE_CORE_COMPOSE_H
