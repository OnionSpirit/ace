/**
 * @file promise.h
 * @brief Promise traits, return-value specializations, and lifecycle states
 *        for ACE coroutines.
 *
 * @details This file defines the building blocks that the C++20 coroutine
 * machinery requires from a promise type:
 *
 *  - <b>@c promise_touch_result</b> — lifecycle state enum shared between
 *    the runner and the coroutine.
 *  - <b>@c permanent / @c differed</b> — policy tags that control whether a
 *    coroutine suspends at creation (@c differed, used by @c ace::async) or
 *    runs immediately (@c permanent, used by @c ace::promise).
 *  - <b>@c promise_return_traits<P, T></b> — CRTP mixin that adds
 *    @c return_value() / @c yield_value() / @c return_void() to a promise type.
 *  - <b>@c promise_traits<T></b> — full promise base that aggregates return
 *    traits, @c await_transform() overloads for all future concepts,
 *    intrusive @c operator new / @c operator delete for control-block
 *    prefix allocation, and optional tracing support.
 *
 * @see ace::coroutines::context, ace::coroutines::control_block
 */
#ifndef ACE_PROMISE_H
#define ACE_PROMISE_H

#include <concepts>
#include <coroutine>
#include <type_traits>
#include <optional>

#include "ace/core/traits/future.h"
#include "ace/core/control.h"
#include "ace/core/tools/meta.h"
#include "ace/core/tools/dispatch.h"
#include "ace/core/tools/id_alloc.h"

namespace ace::core {

    /**
     * @brief Lifecycle state of a coroutine promise.
     *
     * @details The runner inspects this value after each call to @c awake() to
     * decide what to do with the coroutine frame next.
     */
    enum promise_touch_result : uint8_t  {
        e_failed,               ///< Coroutine terminated via unhandled exception.
        e_inited,               ///< Coroutine was just created; runner pool not yet assigned.
        e_executed,             ///< Coroutine is suspended normally (awaiting a future).
        e_executed_with_value,  ///< Coroutine yielded a value and is suspended.
        e_finished,             ///< Coroutine reached @c co_return successfully.
        e_detached,             ///< Coroutine was cancelled and should be dropped.
    };

    /// @brief CRTP tag base for promise suspension policy types.
    struct promise_rule_traits { struct e_promise_rule {}; };

    /**
     * @brief Eager suspension policy — coroutine starts executing immediately.
     *
     * @details Used by @c ace::promise<T>. @c initial_suspend() returns
     * @c std::suspend_never, so the coroutine body runs as soon as the
     * return object is constructed.
     */
    struct permanent : promise_rule_traits {
        /// @brief Returns @c std::suspend_never — no suspension at creation.
        consteval static auto action() noexcept { return std::suspend_never{}; };
    };

    /**
     * @brief Lazy suspension policy — coroutine suspends at creation.
     *
     * @details Used by @c ace::async<T>. @c initial_suspend() returns
     * @c std::suspend_always, so the coroutine does not run until it is
     * explicitly scheduled or awaited.
     */
    struct differed : promise_rule_traits {
        /// @brief Returns @c std::suspend_always — suspends on creation.
        consteval static auto action() noexcept { return std::suspend_always{}; };
    };

    /**
     * @brief Concept that validates a promise suspension policy tag.
     *
     * @details A type satisfies @c is_promise_rule if:
     *  1. It nests @c e_promise_rule (inherits from @c promise_rule_traits).
     *  2. Its static @c action() returns either @c std::suspend_never or
     *     @c std::suspend_always.
     *
     * @tparam modeT  The suspension policy type to check.
     */
    template <typename modeT>
    concept is_promise_rule = requires { typename modeT::e_promise_rule; }
        and (std::same_as<decltype(modeT::action()), std::suspend_never>
        or std::same_as<decltype(modeT::action()), std::suspend_always>);

}

namespace ace::core::traits {

    /**
     * @brief CRTP mixin that provides @c return_value() and @c yield_value()
     *        to a promise type for non-void coroutines.
     *
     * @details This specialization handles coroutines that return a value via
     * @c co_return expr or produce intermediate values via @c co_yield expr.
     *
     * @tparam promiseT  The concrete derived promise type (CRTP).
     * @tparam returnT   The value type returned by @c co_return.
     */
    template <typename promiseT, typename returnT>
    struct promise_return_traits {

        alignas(ACE_BUS_SIZE) promiseT* _derived = static_cast<promiseT*>(this); ///< CRTP pointer to the concrete promise.

        alignas(ACE_BUS_SIZE) returnT _return_value {};                           ///< Storage for the value produced by @c co_return.

        alignas(ACE_BUS_SIZE) promise_touch_result _status { e_inited };          ///< Current lifecycle state.

        /**
         * @brief Called by the coroutine machinery when @c co_return expr is executed.
         * @details Stores the value and transitions status to @c e_finished.
         * @param return_value  Value produced by @c co_return.
         * @return @c std::suspend_never — no suspension after returning.
         */
        auto return_value(returnT return_value) {
            _return_value = std::forward<std::remove_reference_t<returnT>>(return_value);
            _derived->_status = promise_touch_result::e_finished;
            return std::suspend_never{};
        }

        /**
         * @brief Called by the coroutine machinery when @c co_yield expr is executed.
         * @details Stores the intermediate value and transitions status to
         * @c e_executed_with_value, then suspends the coroutine.
         * @param yield_value  Value produced by @c co_yield.
         * @return @c std::suspend_always — suspends after yielding.
         */
        auto yield_value(returnT yield_value) {
            _derived->_status = promise_touch_result::e_executed_with_value;
            _return_value = yield_value;
            return std::suspend_always{};
        }
    };

    /**
     * @brief CRTP mixin specialization for @c void-returning coroutines.
     *
     * @details Provides @c return_void() instead of @c return_value().
     *
     * @tparam promiseT  The concrete derived promise type (CRTP).
     */
    template <typename promiseT>
    struct promise_return_traits <promiseT, void> {

        alignas(ACE_BUS_SIZE) promiseT* _derived = static_cast<promiseT*>(this); ///< CRTP pointer to the concrete promise.

        alignas(ACE_BUS_SIZE) promise_touch_result _status { e_inited };          ///< Current lifecycle state.

        /**
         * @brief Called by the coroutine machinery when @c co_return (no value) is executed.
         * @return @c std::suspend_never — no suspension.
         */
        auto return_void() { return std::suspend_never{}; }
    };

    /**
     * @brief Full promise base class for ACE coroutines.
     *
     * @details @c promise_traits<T, U> combines:
     *  - Return-value machinery from @c promise_return_traits.
     *  - @c await_transform() overloads that route @c co_await expressions to
     *    the appropriate future concept (@c is_future vs @c is_busy_future).
     *  - <b>Intrusive memory layout</b>: @c operator new allocates a
     *    @c control_block immediately before the promise, enabling external
     *    handles without a separate allocation.
     *  - Optional tracing support via @c setup_trace().
     *
     * @tparam derived_t  Derived type of inherited trait user
     *
     * @tparam return_t  The value type returned by @c co_return inside the
     *                   coroutine.  Use @c void for coroutines that do not
     *                   return a value.
     *
     * @par Memory layout
     * @code
     * [ control_block | promise_traits<T> | coroutine frame ]
     *  ▲                ▲
     *  base_ptr         mem_ptr  (returned by operator new)
     * @endcode
     */
    template <typename derived_t, typename return_t>
    struct promise_traits : promise_return_traits<promise_traits<derived_t, return_t>, return_t> {

        typedef traits::future_handle* future_handler_ptr_t; ///< Pointer type for the currently awaited busy future.

        typedef promise_return_traits<promise_traits, return_t> promise_return_traits_t;
        using promise_return_traits_t::_status;

        promise_traits() = default;

        /**
         * @brief Destructor.  Releases the tracing ID if one was allocated.
         */
        ~promise_traits() {
            if (_trace_id) [[unlikely]]
                tools::context_id_allocator::get_instance().id_free(_trace_id.value());
        };

        /**
         * @brief Pass-through for @c co_await std::suspend_always{}.
         * @details Clears the busy future pointer and marks status as
         * @c e_executed.
         * @param e  The suspend object.
         * @return The same @c std::suspend_always value.
         */
        std::suspend_always await_transform(const std::suspend_always& e) {
            _status = e_executed;
            _busy_future = nullptr;
            return e;
        }

        /**
         * @brief Pass-through for @c co_await std::suspend_never{}.
         * @param e  The suspend object.
         * @return The same @c std::suspend_never value.
         */
        std::suspend_never await_transform(const std::suspend_never& e) {
            _status = e_executed;
            _busy_future = nullptr;
            return e;
        }

        /**
         * @brief @c await_transform for lvalue-ref futures (@c is_future concept).
         * @details Resets @c _busy_future because a regular future takes over
         * forwarding control via the conductor mechanism.
         * @tparam futureT  A type satisfying @c ace::core::misc::dispatch::is_future.
         * @param future    The future to await.
         * @return          The same lvalue reference.
         */
        template <typename futureT>
        requires tools::dispatch::is_future<std::remove_reference_t<futureT>, derived_t>
        futureT& await_transform(futureT& future) {
            _status = e_executed;
            _busy_future = nullptr;
            return future;
        }

        /**
         * @brief @c await_transform for rvalue-ref futures (@c is_future concept).
         * @tparam futureT  A type satisfying @c ace::core::misc::dispatch::is_future.
         * @param future    The future to await.
         * @return          An rvalue reference to the future.
         */
        template <typename futureT>
        requires tools::dispatch::is_future<std::remove_reference_t<futureT>, derived_t>
        futureT&& await_transform(futureT&& future) {
            _status = e_executed;
            _busy_future = nullptr;
            return future;
        }

        /**
         * @brief @c await_transform for lvalue-ref busy futures (@c is_busy_future).
         * @details Sets @c _busy_future so the runner can call @c await_ready()
         * repeatedly before re-queuing the task (active polling).
         * @tparam futureT  A type satisfying @c ace::common::dispatch::is_busy_future.
         * @param future    The busy future to await.
         * @return          The same lvalue reference.
         */
        template <typename futureT>
        requires tools::dispatch::is_busy_future<std::remove_reference_t<futureT>, derived_t>
        futureT& await_transform(futureT& future) {
            _status = e_executed;
            _busy_future = &future;
            return future;
        }

        /**
         * @brief @c await_transform for rvalue-ref busy futures (@c is_busy_future).
         * @tparam futureT  A type satisfying @c ace::common::dispatch::is_busy_future.
         * @param future    The busy future to await.
         * @return          An rvalue reference to the future.
         */
        template <typename futureT>
        requires tools::dispatch::is_busy_future<std::remove_reference_t<futureT>, derived_t>
        futureT&& await_transform(futureT&& future) {
            _status = e_executed;
            _busy_future = &future;
            return std::forward<futureT>(future);
        }

        /**
         * @brief Custom allocator that prepends a @c control_block before the promise.
         * @details Allocates @c mem_size + sizeof(control_block) bytes, constructs
         * a @c control_block at the beginning, then returns a pointer offset by
         * @c sizeof(control_block).  This enables external handles without a
         * separate heap allocation.
         * @param mem_size  Requested size for the promise itself.
         * @return Pointer to the promise area (after the control block).
         */
        void* operator new(size_t mem_size) noexcept {
            const auto ptr = static_cast<uint8_t*>(::operator new(mem_size + control_block_size));
            void* mem_ptr = ptr + control_block_size;
            new (ptr) control_block();
            return mem_ptr;
        }

        /**
         * @brief Custom deallocator.  Decrements the control-block strong
         * reference count and frees the whole allocation only when untracked.
         * @param mem_ptr  Pointer to the promise area.
         */
        void operator delete(void* mem_ptr) noexcept {
            void* base_ptr = control_block::get_block_from_address(mem_ptr);
            // NOTE: Trying to disown, and if it's untracked do delete
            if (control_block::disown(base_ptr))
                delete static_cast<control_block*>(base_ptr);
        }

        /**
         * @brief Allocates a unique trace ID for this coroutine instance.
         * @details Useful for debugging and profiling.  The ID is released
         * automatically in the destructor.
         * @return The allocated trace ID.
         */
        std::size_t setup_trace() {
            _trace_id = tools::context_id_allocator::get_instance().id_alloc();
            return _trace_id.value();
        }

        future_handler_ptr_t        _busy_future { nullptr };  ///< Pointer to the currently active busy future, or @c nullptr.
        control_block*              _block  { nullptr };       ///< Pointer to the intrusive control block (set on first @c observe()).
        std::optional<std::size_t>  _trace_id;                 ///< Optional debugging trace ID.
    };

#define DECLARE_PROMISE_TRAITS(derived_t, return_type_t) typedef ace::core::traits::promise_traits<derived_t, return_type_t> promise_traits_t;

#define IMPORT_PROMISE_TRAITS_ENV               \
    using promise_traits_t::_busy_future;       \
    using promise_traits_t::_block;             \
    using promise_traits_t::_status;

}

#endif // ACE_PROMISE_H
