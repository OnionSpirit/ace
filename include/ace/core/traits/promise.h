/**
 * @file promise.h
 * @brief Promise traits, return-value specializations, and lifecycle states
 *        for ACE coroutines.
 *
 * @details This file defines the building blocks that the C++20 coroutine
 * machinery requires from a promise type:
 *
 *  - <b>@c promise_lifecycle</b> — lifecycle state enum shared between
 *    the runner and the coroutine.
 *  - <b>@c lazy_rule / @c eager_rule / @c automaton_rule</b> — rule bases that
 *    control whether a coroutine suspends at creation (@c lazy_rule, used by
 *    @c ace::async) or runs immediately (@c eager_rule, used by
 *    @c ace::promise), and that add @c return_value() / @c yield_value() /
 *    @c return_void() to a promise type.
 *  - <b>@c promise_traits<derived_t, promise_rule_t, return_t></b> — full
 *    promise base that aggregates the rule base,
 *    @c await_transform() overloads for all future concepts,
 *    intrusive @c operator new / @c operator delete for control-block
 *    prefix allocation, and optional tracing support.
 *
 * @see ace::async, ace::core::control_block
 */
#ifndef ACE_PROMISE_H
#define ACE_PROMISE_H

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstring>
#include <memory>
#include <coroutine>
#include <limits>
#include <type_traits>
#include <optional>

#include "ace/core/traits/future.h"
#include "ace/core/control.h"
#include "ace/core/tools/macro.h"
#include "ace/core/tools/id_alloc.h"
#include "ace/core/arena.h"

namespace ace::core {


    /**
     * @brief Base that exposes the control-block pointer and the lifecycle
     *        status accessors to promise types.
     */
    struct promise_primitives {

        control_block*     _block  { nullptr };  ///< Pointer to the intrusive control block (set on coroutine construction).

        /**
         * @brief Read the current lifecycle status from the control block.
         * @return Current @c promise_lifecycle value.
         * @throw std::runtime_error if the control block pointer is null.
         */
        [[nodiscard]] promise_lifecycle status() {
            if (not _block)
                throw std::runtime_error("trying to get status from a frame control block that is null");
            return _block->_status;
        }

        /**
         * @brief Write a new lifecycle status to the control block.
         * @param status  New @c promise_lifecycle value.
         * @return The stored status value.
         * @throw std::runtime_error if the control block pointer is null.
         */
        promise_lifecycle status(const promise_lifecycle status) {
            if (not _block)
                throw std::runtime_error("trying to set status to a frame control block that is null");
            return _block->_status = status;
        }
    };

    // TODO: Move async_routers to the rules

    /**
     * @brief Base that provides @c return_value() and @c yield_value()
     *        to a promise type for non-void coroutines.
     *
     * @details This specialization handles coroutines that return a value via
     * @c co_return expr or produce intermediate values via @c co_yield expr.
     *
     * @tparam returnT   The value type returned by @c co_return.
     */
    template <typename returnT>
    struct automaton_rule : promise_primitives {

        static_assert(not std::is_void_v<returnT>, "It is forbidden to create an <automaton> with <void> return type");

        alignas(ACE_BUS_SIZE) returnT _return_value {}; ///< Storage for the value produced by @c co_return.

        /**
         * @brief Called by the coroutine machinery when @c co_return expr is executed.
         * @details Stores the value and transitions status to @c e_finished.
         * @param return_value  Value produced by @c co_return.
         * @return @c std::suspend_never — no suspension after returning.
         */
        auto return_value(returnT return_value) {
            _return_value = std::forward<std::remove_reference_t<returnT>>(return_value);
            status(e_finished);
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
            status(e_executed_with_value);
            _return_value = yield_value;
            return std::suspend_always{};
        }

        /// @brief Returns @c std::suspend_always — suspends at creation.
        consteval static auto initial_result() noexcept { return std::suspend_always{}; };
    };


    /**
     * @brief Base that provides @c return_value()
     *        to a promise type for lazy non-void coroutines.
     *
     * @details This specialization handles lazy coroutines that return a value via
     * @c co_return expr.
     * Used by @c ace::async<T>. @c initial_suspend() returns
     * @c std::suspend_always, so the coroutine does not run until it is
     * explicitly scheduled or awaited.
     *
     * @tparam returnT   The value type returned by @c co_return.
     */
    template <typename returnT>
    struct lazy_rule : promise_primitives {

        alignas(ACE_BUS_SIZE) returnT _return_value {}; ///< Storage for the value produced by @c co_return.

        /**
         * @brief Called by the coroutine machinery when @c co_return expr is executed.
         * @details Stores the value and transitions status to @c e_finished.
         * @param return_value  Value produced by @c co_return.
         * @return @c std::suspend_never — no suspension after returning.
         */
        auto return_value(returnT return_value) {
            _return_value = std::forward<std::remove_reference_t<returnT>>(return_value);
            status(e_finished);
            return std::suspend_never{};
        }

        /// @brief Returns @c std::suspend_always — suspends at creation (lazy).
        consteval static auto initial_result() noexcept { return std::suspend_always{}; };
    };


    /**
     * @brief Base specialization for @c void-returning lazy coroutines.
     *
     * @details This specialization handles lazy coroutines that return @c void.
     * Provides @c return_void() instead of @c return_value().
     * Used by @c ace::async<void>. @c initial_suspend() returns
     * @c std::suspend_always, so the coroutine does not run until it is
     * explicitly scheduled or awaited.
     *
     */
    template <>
    struct lazy_rule<void> : promise_primitives {

        /**
         * @brief Called by the coroutine machinery when @c co_return (no value) is executed.
         * @return @c std::suspend_never — no suspension.
         */
        auto return_void() {
            status(e_finished);
            return std::suspend_never{};
        }

        /// @brief Returns @c std::suspend_always — suspends at creation (lazy).
        consteval static auto initial_result() noexcept { return std::suspend_always{}; };
    };


    /**
     * @brief Base that provides @c return_value() and @c yield_value()
     *        to a promise type for non-void eager coroutines.
     *
     * @details This specialization handles eager coroutines that return a value via
     * @c co_return expr.
     * Used by @c ace::promise<T>. @c initial_suspend() returns
     * @c std::suspend_never, so the coroutine body runs as soon as the
     * return object is constructed.
     *
     * @tparam returnT   The value type returned by @c co_return.
     */
    template <typename returnT>
    struct eager_rule : promise_primitives {

        alignas(ACE_BUS_SIZE) returnT _return_value {}; ///< Storage for the value produced by @c co_return.

        /**
         * @brief Called by the coroutine machinery when @c co_return expr is executed.
         * @details Stores the value and transitions status to @c e_finished.
         * @param return_value  Value produced by @c co_return.
         * @return @c std::suspend_never — no suspension after returning.
         */
        auto return_value(returnT return_value) {
            _return_value = std::forward<std::remove_reference_t<returnT>>(return_value);
            status(e_finished);
            return std::suspend_never{};
        }

        /// @brief Returns @c std::suspend_never — no suspension at creation.
        consteval static auto initial_result() noexcept { return std::suspend_never{}; };
    };


    /**
     * @brief CRTP mixin specialization for @c void-returning eager coroutines.
     *
     * @details This specialization handles eager coroutines that return @c void.
     * Used by @c ace::promise<void>. @c initial_suspend() returns
     * @c std::suspend_never, so the coroutine body runs as soon as the
     * return object is constructed.
     *
     */
    template <>
    struct eager_rule<void> : promise_primitives {

        /**
         * @brief Called by the coroutine machinery when @c co_return (no value) is executed.
         * @return @c std::suspend_never — no suspension.
         */
        auto return_void() {
            status(e_finished);
            return std::suspend_never{};
        }

        /// @brief Returns @c std::suspend_never — no suspension at creation.
        consteval static auto initial_result() noexcept { return std::suspend_never{}; };
    };

    /**
     * @brief Concept that validates a coroutine rule object.
     *
     * @details A type satisfies @c is_rule if:
     *  1. Its @c std::monostate instantiation derives from @c promise_primitives.
     *  2. Its static @c initial_result() returns either @c std::suspend_never or
     *     @c std::suspend_always.
     *
     * @tparam rule_t  The coroutine rule type to check.
     */
    template <template <typename> typename rule_t>
    concept is_rule =
        std::derived_from<rule_t<std::monostate>, promise_primitives>
        and
        (
            std::same_as<decltype(rule_t<std::monostate>::initial_result()), std::suspend_never>
            or
            std::same_as<decltype(rule_t<std::monostate>::initial_result()), std::suspend_always>
        );

    /**
     * @brief Concept that validates a rule that allowed to be spawned.
     *
     * @tparam rule_t  The coroutine rule type to check.
     */
    template <template <typename> typename rule_t>
    concept is_spawnable_rule =
        std::same_as<rule_t<std::monostate>, automaton_rule<std::monostate>> or
        std::same_as<rule_t<std::monostate>, lazy_rule<std::monostate>>;

    /**
     * @brief Concept that validates a rule that is a automaton.
     *
     * @tparam rule_t  The coroutine rule type to check.
     */
    template <template <typename> typename rule_t>
    concept is_automaton_rule =
        std::same_as<rule_t<std::monostate>, automaton_rule<std::monostate>>;

}

namespace ace::core::traits {

    /**
     * @brief Full promise base class for ACE coroutines.
     *
     * @details @c promise_traits<derived_t, promise_rule_t, return_t> combines:
     *  - Return-value machinery from the rule base @c promise_rule_t<return_t>.
     *  - @c await_transform() overloads that route @c co_await expressions to
     *    the appropriate future concept (@c is_future_accurate vs
     *    @c is_busy_future_accurate).
     *  - <b>Intrusive memory layout</b>: @c operator new allocates a
     *    @c control_block immediately before the compiler-owned coroutine
     *    frame, enabling external handles without a separate allocation.
     *  - Optional tracing support via @c setup_trace().
     *
     * @tparam derived_t  Derived type of inherited trait user
     *
     * @tparam promise_rule_t Rule type that defines initial and return actions
     *
     * @tparam return_t  The value type returned by @c co_return inside the
     *                   coroutine.  Use @c void for coroutines that do not
     *                   return a value.
     *
     * @par Memory layout
     * @code
     * [ control_block | compiler-owned coroutine frame ]
     *  ▲               ▲
     *  block_ptr       frame_ptr  (returned by operator new)
     * @endcode
     */
    template <typename derived_t, template <typename> typename promise_rule_t, typename return_t>
    requires is_rule<promise_rule_t>
    struct promise_traits : promise_rule_t<return_t> {

        typedef future_handle* future_handler_ptr_t; ///< Pointer type for the currently awaited busy future.

        typedef promise_rule_t<return_t> rule_t;

        using rule_t::status;

        /** @brief Default constructor. */
        promise_traits() = default;

        /**
         * @brief Destructor.  Releases the tracing ID if one was allocated.
         */
        ~promise_traits() {
            if (_trace_id) [[unlikely]]
                tools::async_id_allocator::get_instance().id_free(_trace_id.value());
        };

        /**
         * @brief Pass-through for @c co_await std::suspend_always{}.
         * @details Clears the busy future pointer and marks status as
         * @c e_executed.
         * @param e  The suspend object.
         * @return The same @c std::suspend_always value.
         */
        std::suspend_always await_transform(const std::suspend_always& e) {
            status(e_executed);
            _busy_future = nullptr;
            static_cast<derived_t*>(this)->begin_op();
            return e;
        }

        /**
         * @brief Pass-through for @c co_await std::suspend_never{}.
         * @param e  The suspend object.
         * @return The same @c std::suspend_never value.
         */
        std::suspend_never await_transform(const std::suspend_never& e) {
            status(e_executed);
            _busy_future = nullptr;
            static_cast<derived_t*>(this)->begin_op();
            return e;
        }

        /**
         * @brief @c await_transform for lvalue-ref futures (@c is_future concept).
         * @details Resets @c _busy_future because a regular future takes over
         * forwarding control via the router mechanism.
         * @tparam futureT  A type satisfying @c ace::core::meta::is_future_accurate.
         * @param future    The future to await.
         * @return          The same lvalue reference.
         */
        template <typename futureT>
        requires meta::is_future_accurate<std::remove_reference_t<futureT>, derived_t>
        futureT& await_transform(futureT& future) {
            status(e_executed);
            _busy_future = nullptr;
            static_cast<derived_t*>(this)->begin_op();
            return future;
        }

        /**
         * @brief @c await_transform for rvalue-ref futures (@c is_future concept).
         * @tparam futureT  A type satisfying @c ace::core::meta::is_future_accurate.
         * @param future    The future to await.
         * @return          An rvalue reference to the future.
         */
        template <typename futureT>
        requires meta::is_future_accurate<std::remove_reference_t<futureT>, derived_t>
        futureT&& await_transform(futureT&& future) {
            status(e_executed);
            _busy_future = nullptr;
            static_cast<derived_t*>(this)->begin_op();
            return future;
        }

        /**
         * @brief @c await_transform for lvalue-ref busy futures (@c is_busy_future).
         * @details Sets @c _busy_future so the runner can call @c await_ready()
         * repeatedly before re-queuing the task (active polling).
         * @tparam futureT  A type satisfying @c ace::core::meta::is_busy_future_accurate.
         * @param future    The busy future to await.
         * @return          The same lvalue reference.
         */
        template <typename futureT>
        requires meta::is_busy_future_accurate<std::remove_reference_t<futureT>, derived_t>
        futureT& await_transform(futureT& future) {
            status(e_executed);
            _busy_future = &future;
            static_cast<derived_t*>(this)->begin_op();
            return future;
        }

        /**
         * @brief @c await_transform for rvalue-ref busy futures (@c is_busy_future).
         * @tparam futureT  A type satisfying @c ace::core::meta::is_busy_future_accurate.
         * @param future    The busy future to await.
         * @return          An rvalue reference to the future.
         */
        template <typename futureT>
        requires meta::is_busy_future_accurate<std::remove_reference_t<futureT>, derived_t>
        futureT&& await_transform(futureT&& future) {
            status(e_executed);
            _busy_future = &future;
            static_cast<derived_t*>(this)->begin_op();
            return std::forward<futureT>(future);
        }

        /**
         * @brief Allocate an aligned coroutine frame with an adjacent control block.
         * @details The frame base is aligned to
         * @c max(arena::kAlignment,std::bit_floor(mem_size)). The compiler supplies
         * only the frame size in C++23, so over-aligned locals require a compatible
         * compiler frame layout; this is not a guarantee for arbitrary explicitly
         * aligned scalar locals. The arena payload contains alignment padding and
         * a saved raw pointer before the control block. The immutable
         * @c _frame_size covers only the contiguous block and compiler frame,
         * allowing prefetch to stay inside that span.
         * @param mem_size Compiler-requested coroutine frame size.
         * @return Aligned frame address immediately after its control block.
         * @throws std::bad_alloc if metadata or allocation arithmetic overflows,
         *         or arena allocation fails.
         */
        void* operator new(size_t mem_size) {
            constexpr auto max_size = std::numeric_limits<size_t>::max();
            constexpr auto max_frame_size = std::numeric_limits<uint32_t>::max();
            if (mem_size > max_size - control_block_size)
                throw std::bad_alloc {};
            const auto frame_size = mem_size + control_block_size;
            if (frame_size > max_frame_size)
                throw std::bad_alloc {};
            const auto alignment = std::max(arena::kAlignment, std::bit_floor(mem_size));
            if (frame_size > max_size - sizeof(void*))
                throw std::bad_alloc {};
            const auto prefixed_size = frame_size + sizeof(void*);
            if (prefixed_size > max_size - (alignment - 1))
                throw std::bad_alloc {};
            const auto allocation_size = prefixed_size + alignment - 1;
            void* raw = arena::get_instance().allocate(allocation_size);
            void* frame = static_cast<uint8_t*>(raw) + sizeof(void*) + control_block_size;
            auto space = allocation_size - sizeof(void*) - control_block_size;
            std::align(alignment, mem_size, frame, space);
            auto* prefix = static_cast<uint8_t*>(frame) - control_block_size;
            // The saved pointer can be unaligned; memcpy avoids typed access there.
            std::memcpy(prefix - sizeof(void*), &raw, sizeof(raw));
            auto* block = new (prefix) control_block();
            block->_frame_size = frame_size;
            return frame;
        }

        /**
         * @brief Release the original arena payload after the final reference.
         * @details Recovers the saved raw pointer before the control block, keeping
         * the arena's pooled/transient and cross-thread release protocol intact.
         * @param mem_ptr Pointer returned by the frame allocator.
         * @param mem_size Compiler-provided frame size (arena reads its own header).
         */
        void operator delete(void* mem_ptr, size_t mem_size) noexcept {
            // NOTE: Delete only after the owning async and all observers release the block.
            if (control_block* block = control_block::get_block_from_address(mem_ptr); control_block::is_untracked(block)) {
                void* raw;
                std::memcpy(&raw, reinterpret_cast<uint8_t*>(block) - sizeof(void*), sizeof(raw));
                block->~control_block();
                arena::get_instance().deallocate(raw, mem_size);
            }
        }

        /**
         * @brief Allocates a unique trace ID for this coroutine instance.
         * @details Useful for debugging and profiling.  The ID is released
         * automatically in the destructor.
         * @return The allocated trace ID.
         */
        std::size_t setup_trace() {
            _trace_id = tools::async_id_allocator::get_instance().id_alloc();
            return _trace_id.value();
        }

        future_handler_ptr_t        _busy_future  { nullptr };  ///< Pointer to the currently active busy future, or @c nullptr.
        std::optional<std::size_t>  _trace_id;                  ///< Optional debugging trace ID.
    };

    /** @brief Declares a @c promise_traits_t typedef for a derived promise type. */
#define DECLARE_PROMISE_TRAITS(derived_t, promise_rule_t, return_type_t) typedef ace::core::traits::promise_traits<derived_t, promise_rule_t, return_type_t> promise_traits_t;

    /** @brief Imports the @c promise_traits environment: busy future pointer, control block and status. */
#define IMPORT_PROMISE_TRAITS_ENV               \
    using promise_traits_t::_busy_future;       \
    using promise_traits_t::_block;             \
    using promise_traits_t::status;

}

#endif // ACE_PROMISE_H
