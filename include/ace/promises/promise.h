/**
 * @file
 * @details This file contains a future_handler, future_trait classes and its
 * dispatching concepts: is_future_accept_promise, is_future_accept_coroutine,
 * is_future. Types are intended to be used to create derived future objects,
 * that will be processed by co_await operator
 */
#ifndef ACE_PROMISE_H
#define ACE_PROMISE_H

#include "ace/futures/future.h"
#include "ace/futures/command.h"
#include "ace/common/dispatch.h"
#include "ace/hub/hub.h"

#include <concepts>
#include <coroutine>
#include <memory>
#include <type_traits>

namespace ace::promises {

    enum promise_touch_result : uint8_t  {
        e_failed,
        e_blocked,
        e_executed,
        e_executed_with_value,
        e_finished,
    };

    struct promise_rule_traits { struct e_promise_rule {}; };

    struct permanent : promise_rule_traits {
        consteval static auto action() noexcept { return std::suspend_never{}; };
    };

    struct differed : promise_rule_traits {
        consteval static auto action() noexcept { return std::suspend_always{}; };
    };

    template <typename modeT>
    concept is_promise_rule = requires { typename modeT::e_promise_rule; }
        and (std::same_as<decltype(modeT::action()), std::suspend_never>
        or std::same_as<decltype(modeT::action()), std::suspend_always>);


    template <typename promiseT, typename returnT>
    struct promise_return_traits {

        promiseT* _derived = static_cast<promiseT*>(this);

        promise_touch_result _status { e_blocked };

    public:

        returnT _return_value {};

        auto return_value(returnT return_value) {
            _return_value =return_value;
            _derived->_status = promise_touch_result::e_finished;
            return std::suspend_never{};
        }

        auto yield_value(returnT yield_value) {
            _derived->_status = promise_touch_result::e_executed_with_value;
            _return_value =yield_value;
            return std::suspend_always{};
        }
    };


    template <typename promiseT>
    struct promise_return_traits <promiseT, void> {

        promiseT* _derived = static_cast<promiseT*>(this);

        promise_touch_result _status { e_blocked };

    public:

        auto return_void() { return std::suspend_never{}; }
    };

    template <typename returnT, typename hubT>
        struct promise_traits : public promise_return_traits<promise_traits<returnT, hubT>, returnT> {

        typedef ace::futures::future_handler* future_handler_ptr_t;
        typedef promise_return_traits<promise_traits, returnT> promise_return_traits_t;
        using promise_return_traits_t::_status;

        promise_traits() =default;

        ~promise_traits() =default;

        std::suspend_always await_transform(std::suspend_always& e) { return e; }

        std::suspend_never await_transform(std::suspend_never& e) { return e; }

        template <typename futureT>
        requires ace::common::dispatch::is_future<std::remove_reference_t<futureT>, returnT>
        futureT& await_transform(futureT& future) {
            _status = promise_touch_result::e_blocked;
            _future = &future;
            return future;
        }

        template <typename futureT>
        requires ace::common::dispatch::is_future<std::remove_reference_t<futureT>, returnT>
        futureT&& await_transform(futureT&& future) {
            _status = promise_touch_result::e_blocked;
            _future = &future;
            return std::forward<futureT>(future);
        }

        template <typename commandT>
        requires ace::common::dispatch::is_command<std::remove_reference_t<commandT>, returnT>
        static commandT& await_transform(commandT& command) { return command; }

        template <typename commandT>
        requires ace::common::dispatch::is_command<std::remove_reference_t<commandT>, returnT>
        static commandT&& await_transform(commandT&& command) { return command; }

        /* static inline void* operator new(size_t memsize) noexcept; */

        /* static inline void operator delete(void* memptr, size_t memsize) noexcept; */

        // Note: pointers to actual and runner pool
        hubT* _actual_hub{};
        hubT* _runner_hub{};
        future_handler_ptr_t _future;
    };

#define DECLARE_PROMISE_TRAITS(return_type_t, hub_t) typedef promises::promise_traits<return_type_t, hub_t> promise_traits_t;

#define IMPORT_PROMISE_TRAITS_ENV               \
    using promise_traits_t::_future;            \
    using promise_traits_t::_actual_hub;        \
    using promise_traits_t::_runner_hub;        \
    using promise_traits_t::_status;

}

#endif // ACE_PROMISE_H
