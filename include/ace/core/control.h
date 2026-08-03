/**
 * @file control.h
 * @brief Intrusive control block and its external handle for ACE coroutines.
 *
 * @details Every ACE coroutine promise is allocated with a @c control_block
 * immediately *before* the promise in memory (see @c promise_traits::operator new).
 * This provides a zero-cost way to attach external observers without an
 * additional heap allocation.
 *
 * ### Reference counting
 *
 * The control block uses a dual reference-count scheme:
 *  - @c _strong_refcount — counts coroutine *owners* (always 1: the frame itself).
 *    Decremented by @c disown() when the coroutine finishes.
 *  - @c _weak_refcount — counts *watchers* (@c control_block_handle instances).
 *    Incremented by @c watch(), decremented by @c unwatch().
 *
 * The block is freed only when both counts reach zero.
 *
 * ### Lifecycle
 *
 * @code
 * create coroutine           → control_block { _strong=1, _weak=1, _exists=true }
 * async.observe()          → control_block_handle (watch → _weak=2)
 * coroutine finishes         → disown() (_strong=0, _weak=1, _exists=false)
 * handle destructs / cancel  → unwatch() (_weak=0) → delete block
 * @endcode
 */
#ifndef ACE_CONTROL_H
#define ACE_CONTROL_H

#include <atomic>
#include <cstddef>
#include <cstring>

#include "ace/core/traits/routing.h"
#include "ace/core/tools/macro.h"

namespace ace::core {


    /**
     * @brief Lifecycle state of a coroutine promise.
     *
     * @details The runner inspects this value after each call to @c awake() to
     * decide what to do with the coroutine frame next.
     */
    enum promise_lifecycle : uint8_t  {
        e_failed,               ///< Coroutine terminated via unhandled exception.
        e_inited,               ///< Coroutine was just created; runner pool not yet assigned.
        e_executed,             ///< Coroutine is suspended normally (awaiting a future).
        e_executed_with_value,  ///< Coroutine yielded a value and is suspended.
        e_finished,             ///< Coroutine reached @c co_return successfully.
        e_canceled,             ///< Coroutine was canceled and should be dropped.
    };

    /**
     * @brief Intrusive reference-counted control block for a coroutine promise.
     *
     * @details Allocated immediately before the promise in memory by
     * @c promise_traits::operator new.  Stores the reference counts and an
     * optional pointer to a @c control_router_handle that enables external
     * join / cancel operations.
     *
     * All static methods accept a raw @c void* pointing to @b either the
     * block itself @b or a promise address; @c get_block_from_address converts
     * the latter to the former.
     */
    struct control_block {

        struct {
            uint32_t          _refcount   : 24 { 1 };                ///< Number of watchers (handles). Initial value: 1 (the coroutine itself).
            promise_lifecycle _status     : 8  { e_inited };         ///< Flag that shows that Coroutines completed without cancellation
            uint32_t          _frame_size : 32 { 0 };                ///< Coroutine frame size, including control block. Non-zero value means stack is exist, 0 otherwise
        };
        traits::async_router_handle* _control_router { nullptr };    ///< Optional router for external join/cancel; set by @c setup_control_block().

        control_block() = default;

        ~control_block() = default;

        /**
         * @brief Check reference count is zero.
         * @param v_block  Pointer to the control block.
         * @return @c true if @c _refcount is 0.
         */
        static bool is_untracked(void* v_block);

        /**
         * @brief Increment the refcount.
         * @details Called when a new @c control_block_handle is constructed.
         * @param v_block  Pointer to the control block.
         * @return @c true if the block became untracked after the operation
         *         (only possible if @c refcount was already 0).
         */
        static bool track(void* v_block);

        /**
         * @brief Decrement the refcount.
         * @details Called from @c control_block_handle's destructor or @c cancel().
         * @param v_block  Pointer to the control block.
         * @return @c true if the block became untracked and can be freed.
         */
        static bool untrack(void* v_block);

        /**
         * @brief Convert a promise address to the @c control_block* that precedes it.
         * @param address  Raw promise address returned by @c operator new.
         * @return Pointer to the control block.
         */
        static control_block* get_block_from_address(void* address);

    };

    /// @brief Byte size of @c control_block.  Used as an allocation prefix offset.
    inline constexpr std::size_t control_block_size = sizeof(control_block);

    /**
     * @brief Concept that checks whether a promise type carries a @c control_block*.
     * @tparam promise_t  Promise type to inspect.
     */
    template <typename promise_t>
    concept is_controled_promise = requires (promise_t p) {
        { std::remove_reference_t<decltype(p._block)>{} } -> std::same_as<control_block*>;
    };

    /**
     * @brief Copyable external handle to a coroutine's control block.
     *
     * @details Provides safe @c cancel(), @c done(), and @c forward() operations
     * from outside the scheduler — for example from @c ace::futures::async_handle
     * or user code that calls @c async::observe().
     *
     * Copies increment the weak reference count; destruction decrements it.
     * When the count reaches zero the control block is freed.
     *
     * @warning <b>Not thread-safe.</b>  Do not share a single handle across threads
     * without external synchronization.
     */
    class control_block_handle {

        control_block* _block { nullptr };

        void release() {
            if (control_block::untrack(_block))
                _block->_control_router->destroy();
            _block = nullptr;
        }

    public:

        control_block_handle() = default;

        /**
         * @brief Copy constructor.  Increments the weak reference count.
         * @param h  Handle to copy.
         */
        control_block_handle(const control_block_handle& h) {
            this->_block = h._block;
            control_block::track(_block);
        }

        /**
         * @brief Construct from a coroutine handle whose promise satisfies
         *        @c is_controled_promise.
         * @tparam promise_t  Promise type (must have @c _block member).
         * @param promise  Coroutine handle to observe.
         */
        template <is_controled_promise promise_t>
        explicit control_block_handle(const std::coroutine_handle<promise_t>& promise) {
            _block = promise.promise()._block;
            control_block::track(_block);
        }

        /// @brief Destructor.  Decrements the weak reference count; may delete the block.
        ~control_block_handle() { release(); }

        /**
         * @brief Request cancellation of the associated coroutine.
         * @details Calls @c control_router->cancel(), then releases this handle.
         * No-op if the handle is idle or the coroutine has already finished.
         */
        void cancel() {
            if (is_idle() or not _block->_control_router or done()) [[unlikely]]
                return;
            _block->_control_router->cancel();
            release();
        }

        /// @brief @c true if this handle does not reference any control block.
        [[nodiscard]] bool is_idle() const { return not _block; }

        /**
         * @brief Checks if the associated coroutine stack frame is destroyed.
         * @return @c false if @c is_idle(), otherwise not exists.
         */
        [[nodiscard]] bool done() const {
            if (is_idle()) [[unlikely]] return false;
            return _block->_status == e_failed or
                   _block->_status == e_canceled or
                   _block->_status == e_finished;
        }

        /**
         * @brief Checks if the associated coroutine finished.
         * @return @c true if status is @c finished, otherwise @c false and if handler @c is_idle() .
         */
        [[nodiscard]] bool finished() const {
            if (is_idle()) [[unlikely]] return false;
            return _block->_status == e_finished;
        }

        /**
         * @brief Taking return value from a task
         * @return @c false if associated coroutine not finished, @c true if value captured
         */
        [[nodiscard]] bool return_value(void* mem_ptr) const {
            if (not finished()) [[unlikely]] return false;
            return _block->_control_router->return_value(mem_ptr);
        }

        /**
         * @brief Take a yielded value from automaton coroutine.
         * @details Only succeeds when coroutine is in e_executed_with_value state.
         * @param mem_ptr  Pointer to store the yielded value.
         * @return @c true if value was captured, @c false if not yielded.
         */
        [[nodiscard]] bool yield_value(void* mem_ptr) const {
            if (not _block or not _block->_control_router) [[unlikely]] return false;
            return _block->_control_router->yield_value(mem_ptr);
        }

        /**
         * @brief Check if automaton has a yielded value ready.
         * @details Returns @c true when coroutine is in e_executed_with_value state.
         * @return @c true if a yielded value is available.
         */
        [[nodiscard]] bool has_yield() const {
            if (not _block or not _block->_control_router) [[unlikely]] return false;
            return _block->_control_router->has_yield();
        }

        bool set_yield_waiter(void* node_ptr) const {
            if (not _block or not _block->_control_router) [[unlikely]] return false;
            return _block->_control_router->set_yield_waiter(node_ptr);
        }

        bool cancel_yield() const {
            if (not _block or not _block->_control_router) [[unlikely]] return false;
            return _block->_control_router->cancel_yield();
        }

        /**
         * @brief Register a waiter async to be notified when the coroutine finishes.
         * @param waiter  Pointer to the @c ace::task async to register.
         * @return @c true if the waiter was accepted by the router.
         */
        bool forward(void* waiter) const {
            if (not _block) [[unlikely]] return false;
            if (done() or not _block->_control_router or waiter == nullptr) [[unlikely]]
                return false;
            return _block->_control_router->redirect(waiter);
        }
    };


    // NOTE: Checks if there are no watchers or owners at the control block
    inline bool control_block::is_untracked(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        return  block->_refcount == 0;
    }

    // NOTE: Attaches spectator to the control block
    inline bool control_block::track(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        if (block->_refcount == 0) [[unlikely]] goto end;
        ++block->_refcount;
        end: return is_untracked(block);
    }

    // NOTE: Detaches spectator from the control block
    inline bool control_block::untrack(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        if (block->_refcount == 0) [[unlikely]] goto end;
        --block->_refcount;
        end: return is_untracked(block);
    }

    // NOTE: Gets control block pointer from the raw promise address
    inline control_block* control_block::get_block_from_address(void* address) {
        return reinterpret_cast<control_block*>(static_cast<uint8_t*>(address) - control_block_size);
    }

} // end namespace ace::coroutines

#endif //ACE_CONTROL_H
