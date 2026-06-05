#ifndef ACE_CORE_COMPOSE_H
#define ACE_CORE_COMPOSE_H

#include <optional>
#include <variant>

#include "ace/core/async.h"
#include "ace/core/dispatcher.h"
#include "ace/core/async_handle.h"
#include "ace/core/traits/future.h"

namespace ace::core {

    template <typename l_future_t, typename r_future_t>
    struct ACE_AWAIT_NODISCARD or_await final : traits::future_traits<or_await<l_future_t, r_future_t>> {

        IMPORT_FUTURE_ENV(or_await);

        struct or_await_conductor;
        friend or_await_conductor;

        or_await(l_future_t& l_future, r_future_t& r_future)
            : _l_future(l_future)
            , _r_future(r_future) {};

        static consteval auto define_return_type() {
            typedef meta::resume_type<l_future_t> l_future_ret_t;
            typedef meta::resume_type<r_future_t> r_future_ret_t;
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

        std::optional<task> _waiter;
        l_future_t& _l_future;
        r_future_t& _r_future;
        std::optional<async_handle> _l_future_observer;
        std::optional<async_handle> _r_future_observer;
        return_t _result;

        template <size_t observer_idx, typename future_t>
        task observer(future_t& future, std::optional<async_handle>& opposite_observer) {

            typedef decltype(std::declval<future_t>().await_resume()) future_ret_t;

            if constexpr (not std::same_as<void, future_ret_t> and not std::same_as<return_t, future_ret_t>)
                std::get<observer_idx>(_result) = co_await future;
            else if constexpr (not std::same_as<void, future_ret_t> and std::same_as<return_t, future_ret_t>)
                _result = co_await future;
            else
                co_await future;

            if (opposite_observer)
                opposite_observer->cancel();

            if (_waiter)
                runner::reattach(std::move(_waiter.value()));

            // NOTE: Setting finished operand ID if both operands are void awaitable
            if constexpr (std::same_as<int, return_t>)
                _result = observer_idx;
        };

        bool await_suspend(auto);

        return_t await_resume() { return _result; };
    };

    template <typename l_future_t, typename r_future_t>
    struct ACE_AWAIT_NODISCARD and_await final : traits::future_traits<and_await<l_future_t, r_future_t>> {

        IMPORT_FUTURE_ENV(and_await);

        struct and_await_conductor;
        friend and_await_conductor;

        and_await(l_future_t& l_future, r_future_t& r_future)
            : _l_future(l_future)
            , _r_future(r_future) {};

        static consteval auto define_return_type() {
            typedef meta::resume_type<l_future_t> l_future_ret_t;
            typedef meta::resume_type<r_future_t> r_future_ret_t;
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

        task _waiter;
        l_future_t& _l_future;
        r_future_t& _r_future;
        std::optional<async_handle> _l_future_observer;
        std::optional<async_handle> _r_future_observer;
        return_t _result;

        template <size_t observer_idx, typename future_t>
        task observer(future_t& future, std::optional<async_handle>& opposite_observer) {

            typedef meta::resume_type<future_t> future_ret_t;

            if constexpr (not std::same_as<void, future_ret_t> and not std::same_as<return_t, future_ret_t>)
                std::get<observer_idx>(_result) = co_await future;
            else if constexpr (not std::same_as<void, future_ret_t> and std::same_as<return_t, future_ret_t>)
                _result = co_await future;
            else
                co_await future;

            // NOTE: Only second observer joins and reattaches
            if constexpr (observer_idx == 1)
                if (not opposite_observer.value().done())
                    co_await opposite_observer->join();

            if constexpr (observer_idx == 1)
                runner::reattach(std::move(_waiter));
        };

        bool await_suspend(auto);

        void await_resume() requires std::same_as<std::monostate, return_t> { }

        return_t await_resume() requires (not std::same_as<std::monostate, return_t>) {
            return _result;
        };
    };

    template <meta::is_future ... future_ts>
    struct ACE_AWAIT_NODISCARD or_await_composed final : traits::future_traits<or_await_composed<future_ts...>> {

        IMPORT_FUTURE_ENV(or_await_composed);

        struct or_await_composed_conductor;
        friend or_await_composed_conductor;

        static constexpr int futures_amount = sizeof...(future_ts);
        static constexpr int top_observer_idx = futures_amount - 1;

        explicit or_await_composed(future_ts&... futures)
            : _futures(futures...) {};

        static consteval auto define_return_type() {
            typedef std::tuple<meta::replace_type<meta::resume_type<future_ts>, void, std::monostate>...> temp_ret_t;
            typedef meta::unique_tuple_t<temp_ret_t> ret_tuple_t;
            if constexpr (std::same_as<std::tuple<std::monostate>, ret_tuple_t>)
                return int();
            else
                return meta::tuple_to_variant_t<temp_ret_t>{};
        }

        typedef decltype(define_return_type()) return_t;

        task _waiter;
        std::tuple<future_ts&...> _futures;
        std::array<std::optional<async_handle>, sizeof...(future_ts)> _observers;
        return_t _result;

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

            if (_waiter)
                runner::reattach(std::move(_waiter));

            // NOTE: Setting finished operand ID if both operands are void awaitable
            if constexpr (std::same_as<int, return_t>)
                _result = observer_idx;
        };

        bool await_suspend(auto);

        return_t await_resume() { return _result; };
    };

    template <meta::is_future ... future_ts>
    struct ACE_AWAIT_NODISCARD and_await_composed final : traits::future_traits<and_await_composed<future_ts...>> {

        IMPORT_FUTURE_ENV(and_await_composed);

        struct and_await_composed_conductor;
        friend and_await_composed_conductor;

        static constexpr int futures_amount = sizeof...(future_ts);
        static constexpr int top_observer_idx = futures_amount - 1;

        explicit and_await_composed(future_ts&... futures)
            : _futures(futures...) {};

        static consteval auto define_return_type() {
            typedef std::tuple<meta::replace_type<meta::resume_type<future_ts>>...> temp_ret_t;
            typedef meta::unique_tuple_t<temp_ret_t> ret_tuple_t;
            if constexpr (std::same_as<std::tuple<std::monostate>, ret_tuple_t>)
                return std::monostate{};
            else
                return temp_ret_t{};
        }

        typedef decltype(define_return_type()) return_t;

        task _waiter;
        std::tuple<future_ts&...> _futures;
        std::array<std::optional<async_handle>, sizeof...(future_ts)> _observers;
        return_t _result;

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
                runner::reattach(std::move(_waiter));
        };

        bool await_suspend(auto);

        auto await_resume() {
            if constexpr (std::same_as<return_t, std::monostate>) return;
            else return _result;
        };

    };


// =============================================- OUTPUT COMPOSERS -====================================================

    template <
        meta::is_future sender_t,
        typename async_return, typename async_input,
        is_promise_rule async_promise_rule_t =differed
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
        is_promise_rule async_promise_rule_t =differed
    > requires std::same_as<meta::resume_type<sender_t>, void>
    //
    promise<async_return>
    compose(sender_t&& sender, async<async_return, async_promise_rule_t>(responder)()) {
        co_await sender;
        co_return co_await responder();
    }


    template <
        meta::is_future sender_t,
        typename foo_return, typename foo_input
    > requires (not std::same_as<meta::resume_type<sender_t>, void>)
    //
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
    //
    promise<foo_return>
    compose(sender_t&& sender, foo_return(responder)()) {
        co_await sender;
        co_return responder();
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
struct ACE_OR_AWAIT_FUTURE_SPACE or_await_conductor final : conductor_handler_t {

    or_await_conductor() = delete;

    explicit or_await_conductor(or_await* or_await_)
        : _or_await(or_await_) {};

    void forward(task&& ctx) override {
        _or_await->_waiter = std::move(ctx);
    }

    void cancel() override {
        _or_await->_l_future_observer->cancel();
        _or_await->_r_future_observer->cancel();
    }

    ~or_await_conductor() override = default;

    or_await* _or_await;
};


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
    // NOTE: Setting conductor for external waiter
    external_coro.promise()._runner_conductor = or_await_conductor {this};
    return true;
}


ACE_COMPOSE_AWAIT_FUTURE_META
struct ACE_AND_AWAIT_FUTURE_SPACE and_await_conductor final : conductor_handler_t {

    and_await_conductor() = delete;

    explicit and_await_conductor(and_await* and_await_)
        : _and_await(and_await_) {};

    void forward(task&& ctx) override {
        _and_await->_waiter = std::move(ctx);
    }

    void cancel() override {
        _and_await->_l_future_observer->cancel();
        _and_await->_r_future_observer->cancel();
    }

    ~and_await_conductor() override = default;

    and_await* _and_await;
};


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
    // NOTE: Setting conductor for external waiter
    external_coro.promise()._runner_conductor = and_await_conductor {this};
    return true;
}

ACE_COMPOSE_AWAIT_COMPOSED_FUTURE_META
struct ACE_AND_AWAIT_COMPOSED_FUTURE_SPACE and_await_composed_conductor final : conductor_handler_t {

    and_await_composed_conductor() = delete;

    explicit and_await_composed_conductor(and_await_composed* and_await_composed_)
        : _and_await_composed(and_await_composed_) {};

    void forward(task&& ctx) override {
        _and_await_composed->_waiter = std::move(ctx);
    }

    void cancel() override {
        for (auto& opposite_observer : _and_await_composed->_observers) {
            opposite_observer->cancel();
        }
    }

    ~and_await_composed_conductor() override = default;

    and_await_composed* _and_await_composed;
};


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
    // NOTE: Setting conductor for external waiter
    external_coro.promise()._runner_conductor = and_await_composed_conductor{this};
    return true;
}

ACE_COMPOSE_AWAIT_COMPOSED_FUTURE_META
struct ACE_OR_AWAIT_COMPOSED_FUTURE_SPACE or_await_composed_conductor final : conductor_handler_t {

    or_await_composed_conductor() = delete;

    explicit or_await_composed_conductor(or_await_composed* or_await_composed_)
        : _or_await_composed(or_await_composed_) {};

    void forward(task&& ctx) override {
        _or_await_composed->_waiter = std::move(ctx);
    }

    void cancel() override {
        for (auto& opposite_observer : _or_await_composed->_observers) {
            opposite_observer->cancel();
        }
    }

    ~or_await_composed_conductor() override = default;

    or_await_composed* _or_await_composed;
};


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
    // NOTE: Setting conductor for external waiter
    external_coro.promise()._runner_conductor = or_await_composed_conductor{this};
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

// ========================================- OUTPUT COMPOSE CREATORS -==================================================

template <
    ace::core::meta::is_future sender_t,
    typename async_return, typename async_input,
    ace::core::is_promise_rule async_promise_rule_t =ace::core::differed
> requires (not std::same_as<ace::core::meta::resume_type<sender_t>, void>)
//
ace::promise<async_return>
operator >> (sender_t&& sender, ace::core::async<async_return, async_promise_rule_t>(responder)(async_input)) {
    return std::move(compose(std::forward<sender_t>(sender), responder));
}


template <
    ace::core::meta::is_future sender_t,
    typename async_return,
    ace::core::is_promise_rule async_promise_rule_t =ace::core::differed
> requires std::same_as<ace::core::meta::resume_type<sender_t>, void>
//
ace::promise<async_return>
operator >> (sender_t&& sender, ace::core::async<async_return, async_promise_rule_t>(responder)()) {
    return std::move(compose(std::forward<sender_t>(sender), responder));
}


template <
    ace::core::meta::is_future sender_t,
    typename foo_return, typename foo_input
> requires (not std::same_as<ace::core::meta::resume_type<sender_t>, void>)
//
ace::promise<foo_return>
operator >> (sender_t&& sender, foo_return(responder)(foo_input)) {
    return std::move(compose(std::forward<sender_t>(sender), responder));
}


template <
    ace::core::meta::is_future sender_t,
    typename foo_return
> requires std::same_as<ace::core::meta::resume_type<sender_t>, void>
//
ace::promise<foo_return>
operator >> (sender_t&& sender, foo_return(responder)()) {
    return std::move(compose(std::forward<sender_t>(sender), responder));
}

#undef ACE_COMPOSE_MEMBERS_ASSERT

#endif //ACE_CORE_COMPOSE_H
