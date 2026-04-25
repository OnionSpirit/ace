#ifndef ACE_CORE_COMPOSE_H
#define ACE_CORE_COMPOSE_H

#include <optional>
#include <variant>

#include "ace/core/context.h"
#include "ace/core/dispatcher.h"
#include "ace/core/async_handle.h"
#include "ace/core/traits/future.h"

namespace ace {

    template <typename l_future_t, typename r_future_t>
    struct or_await final : core::traits::future_traits<or_await<l_future_t, r_future_t>> {

        IMPORT_FUTURE_ENV(or_await);

        struct or_await_conductor;
        friend or_await_conductor;

        or_await(l_future_t& l_future, r_future_t& r_future)
            : _l_future(l_future)
            , _r_future(r_future) {};

        static consteval auto define_return_type() {
            typedef decltype(l_future_t{}.await_resume()) l_future_ret_t;
            typedef decltype(r_future_t{}.await_resume()) r_future_ret_t;
            if constexpr (std::same_as<void, l_future_ret_t> and std::same_as<void, r_future_ret_t>)
                return;
            else if constexpr (std::same_as<void, l_future_ret_t> and not std::same_as<void, r_future_ret_t>)
                return std::optional<r_future_ret_t>{};
            else if constexpr (std::same_as<void, r_future_ret_t> and not std::same_as<void, l_future_ret_t>)
                return std::optional<r_future_ret_t>{};
            else return std::variant<l_future_ret_t, r_future_ret_t>{};
        }

        typedef decltype(define_return_type()) return_t;

        task _waiter;
        l_future_t& _l_future;
        r_future_t& _r_future;
        std::optional<futures::async_handle> _l_future_observer;
        std::optional<futures::async_handle> _r_future_observer;
        std::conditional_t<std::same_as<return_t, void>, int, return_t> _result;

        template <typename future_t>
        task observer(future_t& future, std::optional<futures::async_handle>& opposite_observer) {

            typedef decltype(future_t{}.await_resume()) future_ret_t;

            if constexpr (not std::same_as<void, future_ret_t>)
                _result = co_await future;
            else
                co_await future;

            if (opposite_observer)
                opposite_observer->cancel();

            core::runner::reattach(std::move(_waiter));
        };

        bool await_suspend(auto);

        return_t await_resume() {
            if constexpr (std::same_as<return_t, void>)
                return;
            else return _result;
        };
    };

    template <typename l_future_t, typename r_future_t>
    struct and_await final : core::traits::future_traits<and_await<l_future_t, r_future_t>> {

        IMPORT_FUTURE_ENV(and_await);

        struct and_await_conductor;
        friend and_await_conductor;

        and_await(l_future_t& l_future, r_future_t& r_future)
            : _l_future(l_future)
            , _r_future(r_future) {};

        static consteval auto define_return_type() {
            typedef decltype(l_future_t{}.await_resume()) l_future_ret_t;
            typedef decltype(r_future_t{}.await_resume()) r_future_ret_t;
            if constexpr (std::same_as<void, l_future_ret_t> and std::same_as<void, r_future_ret_t>)
                return;
            else if constexpr (std::same_as<void, l_future_ret_t> and not std::same_as<void, r_future_ret_t>)
                return r_future_ret_t{};
            else if constexpr (not std::same_as<void, l_future_ret_t> and std::same_as<void, r_future_ret_t>)
                return l_future_ret_t{};
            else if constexpr (std::same_as<l_future_ret_t, r_future_ret_t>)
                return std::array<l_future_ret_t, 2>{};
            else return std::tuple<l_future_ret_t, r_future_ret_t>{};
        }

        typedef decltype(define_return_type()) return_t;

        task _waiter;
        l_future_t& _l_future;
        r_future_t& _r_future;
        std::optional<futures::async_handle> _l_future_observer;
        std::optional<futures::async_handle> _r_future_observer;
        std::conditional_t<std::same_as<return_t, void>, int, return_t> _result;

        template <size_t result_id, typename future_t>
        task observer(future_t& future, std::optional<futures::async_handle>& opposite_observer) {

            typedef decltype(future_t{}.await_resume()) future_ret_t;

            if constexpr (not std::same_as<void, future_ret_t> and not std::same_as<return_t, future_ret_t>)
                std::get<result_id>(_result) = co_await future;
            else if constexpr (not std::same_as<void, future_ret_t> and std::same_as<return_t, future_ret_t>)
                _result = co_await future;
            else
                co_await future;

            if (opposite_observer)
                co_await opposite_observer->join();

            if constexpr (result_id == 0)
                core::runner::reattach(std::move(_waiter));
        };

        bool await_suspend(auto);

        return_t await_resume() {
            if constexpr (std::same_as<return_t, void>)
                return;
            else return _result;
        };
    };

} // end namespace ace

//==============================- DEFINITIONS -==================================

#define ACE_COMPOSE_AWAIT_FUTURE_META \
    template <typename l_future_t, typename r_future_t>

#define ACE_OR_AWAIT_FUTURE_SPACE \
    ace::or_await<l_future_t, r_future_t>::

#define ACE_OR_AWAIT_FUTURE_MEMBER(return_t) \
    return_t ace::or_await<l_future_t, r_future_t>::

#define ACE_AND_AWAIT_FUTURE_SPACE \
    ace::and_await<l_future_t, r_future_t>::

#define ACE_AND_AWAIT_FUTURE_MEMBER(return_t) \
    return_t ace::and_await<l_future_t, r_future_t>::

ACE_COMPOSE_AWAIT_FUTURE_META
struct ACE_OR_AWAIT_FUTURE_SPACE or_await_conductor final : conductor_handler_t {

    or_await_conductor() = delete;

    explicit or_await_conductor(ace::or_await<l_future_t, r_future_t>* or_await_)
        : _or_await(or_await_) {};

    void forward(ace::task&& ctx) override {
        _or_await->_waiter = std::move(ctx);
    }

    void cancel() override {
        _or_await->_l_future_observer->cancel();
        _or_await->_r_future_observer->cancel();
    }

    ~or_await_conductor() override = default;

    or_await* _or_await;
};

ACE_COMPOSE_AWAIT_FUTURE_META
ACE_OR_AWAIT_FUTURE_MEMBER(bool)
await_suspend(auto external_coro) {
    // NOTE: Creating observers for each futures
    task _l_observer = observer(_l_future, _r_future_observer);
    task _r_observer = observer(_r_future, _l_future_observer);
    // NOTE: Creating Handlers for observation tasks
    _l_future_observer = futures::async_handle {_l_observer.observe()};
    _r_future_observer = futures::async_handle {_r_observer.observe()};
    // NOTE: Scheduling observers
    schedule(std::move(_l_observer), reinterpret_cast<core::runner*>(external_coro.promise()._runner_pool));
    schedule(std::move(_r_observer), reinterpret_cast<core::runner*>(external_coro.promise()._runner_pool));
    // NOTE: Setting conductor for external waiter
    external_coro.promise()._runner_conductor = or_await_conductor {this};
    return true;
}

ACE_COMPOSE_AWAIT_FUTURE_META
struct ACE_AND_AWAIT_FUTURE_SPACE and_await_conductor final : conductor_handler_t {

    and_await_conductor() = delete;

    explicit and_await_conductor(ace::and_await<l_future_t, r_future_t>* and_await_)
        : _and_await(and_await_) {};

    void forward(ace::task&& ctx) override {
        _and_await->_waiter = std::move(ctx);
    }

    void cancel() override {
        _and_await->_l_future_observer->cancel();
        _and_await->_r_future_observer->cancel();
    }

    ~and_await_conductor() override = default;

    and_await* _and_await;
};

ACE_COMPOSE_AWAIT_FUTURE_META
ACE_AND_AWAIT_FUTURE_MEMBER(bool)
await_suspend(auto external_coro) {
    // NOTE: Creating observers for each futures
    task _l_observer = observer<0>(_l_future, _r_future_observer);
    task _r_observer = observer<1>(_r_future, _l_future_observer);
    // NOTE: Creating Handlers for observation tasks
    _l_future_observer = futures::async_handle {_l_observer.observe()};
    _r_future_observer = futures::async_handle {_r_observer.observe()};
    // NOTE: Scheduling observers
    schedule(std::move(_l_observer), reinterpret_cast<core::runner*>(external_coro.promise()._runner_pool));
    schedule(std::move(_r_observer), reinterpret_cast<core::runner*>(external_coro.promise()._runner_pool));
    // NOTE: Setting conductor for external waiter
    external_coro.promise()._runner_conductor = and_await_conductor {this};
    return true;
}

#undef ACE_COMPOSE_AWAIT_FUTURE_META
#undef ACE_AND_AWAIT_FUTURE_MEMBER
#undef ACE_AND_AWAIT_FUTURE_SPACE
#undef ACE_OR_AWAIT_FUTURE_MEMBER
#undef ACE_OR_AWAIT_FUTURE_SPACE

//==============================- OPERATOR DEFINITIONS -=========================

template <typename l_future_t, typename r_future_t>
ace::or_await<l_future_t, r_future_t> operator or(l_future_t&& l_future, r_future_t&& r_future) {
    return ace::or_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t>
ace::or_await<l_future_t, r_future_t> operator or(l_future_t& l_future, r_future_t&& r_future) {
    return ace::or_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t>
ace::or_await<l_future_t, r_future_t> operator or(l_future_t&& l_future, r_future_t& r_future) {
    return ace::or_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t>
ace::or_await<l_future_t, r_future_t> operator or(l_future_t& l_future, r_future_t& r_future) {
    return ace::or_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t>
ace::and_await<l_future_t, r_future_t> operator and(l_future_t&& l_future, r_future_t&& r_future) {
    return ace::and_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t>
ace::and_await<l_future_t, r_future_t> operator and(l_future_t& l_future, r_future_t&& r_future) {
    return ace::and_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t>
ace::and_await<l_future_t, r_future_t> operator and(l_future_t&& l_future, r_future_t& r_future) {
    return ace::and_await{l_future, r_future};
}

template <typename l_future_t, typename r_future_t>
ace::and_await<l_future_t, r_future_t> operator and(l_future_t& l_future, r_future_t& r_future) {
    return ace::and_await{l_future, r_future};
}

#endif //ACE_CORE_COMPOSE_H
