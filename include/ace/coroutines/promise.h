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
#include "ace/common/dispatch.h"

#include <concepts>
#include <coroutine>
#include <type_traits>

#include "ace/common/id_alloca.h"

namespace ace::coroutines {

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

        auto return_void() { return std::suspend_never{}; }
    };

    template <typename return_t>
        struct promise_traits : promise_return_traits<promise_traits<return_t>, return_t> {

        typedef futures::future_handler* future_handler_ptr_t;
        typedef promise_return_traits<promise_traits, return_t> promise_return_traits_t;
        using promise_return_traits_t::_status;

        promise_traits() =default;

        ~promise_traits() { common::context_id_allocator::get_instance().id_free(_identifier); };

        std::suspend_always await_transform(const std::suspend_always& e) {
            _status = e_executed;
            _future = nullptr;
            return e;
        }

        std::suspend_never await_transform(const std::suspend_never& e) {
            _status = e_executed;
            _future = nullptr;
            return e;
        }

        template <typename futureT>
        requires ace::common::dispatch::is_future<std::remove_reference_t<futureT>, return_t>
        futureT& await_transform(futureT& future) {
            _status = e_blocked;
            _future = &future;
            return future;
        }

        template <typename futureT>
        requires ace::common::dispatch::is_future<std::remove_reference_t<futureT>, return_t>
        futureT&& await_transform(futureT&& future) {
            _status = e_blocked;
            _future = &future;
            return std::forward<futureT>(future);
        }

        template <typename commandT>
        requires ace::common::dispatch::is_command<std::remove_reference_t<commandT>, return_t>
        commandT& await_transform(commandT& command) {
            _status = e_executed;
            _future = nullptr;
            return command;
        }

        template <typename commandT>
        requires ace::common::dispatch::is_command<std::remove_reference_t<commandT>, return_t>
        commandT&& await_transform(commandT&& command) {
            _status = e_executed;
            _future = nullptr;
            return command;
        }

        // TODO: Define in future to attach custom allocator
        /* static inline void* operator new(size_t memsize) noexcept; */

        /* static inline void operator delete(void* memptr, size_t memsize) noexcept; */

        future_handler_ptr_t _future { nullptr };
        std::size_t _identifier { common::context_id_allocator::get_instance().id_alloc() };
    };

#define DECLARE_PROMISE_TRAITS(return_type_t) typedef coroutines::promise_traits<return_type_t> promise_traits_t;

#define IMPORT_PROMISE_TRAITS_ENV               \
    using promise_traits_t::_future;            \
    using promise_traits_t::_status;

}

#endif // ACE_PROMISE_H
