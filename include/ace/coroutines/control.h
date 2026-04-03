/**
 * @file control.h
 * @brief Intrusive control block and its external handle for ACE coroutines.
 *
 * @details Every ACE coroutine promise is allocated with a `control_block`
 * immediately *before* the promise in memory (see `promise_traits::operator new`).
 * This provides a zero-cost way to attach external observers without an
 * additional heap allocation.
 *
 * ### Reference counting
 *
 * The control block uses a dual reference-count scheme:
 *  - `_strong_refcount` — counts coroutine *owners* (always 1: the frame itself).
 *    Decremented by `disown()` when the coroutine finishes.
 *  - `_weak_refcount` — counts *watchers* (`control_block_handle` instances).
 *    Incremented by `watch()`, decremented by `unwatch()`.
 *
 * The block is freed only when both counts reach zero.
 *
 * ### Lifecycle
 *
 * @code
 * create coroutine           → control_block { _strong=1, _weak=1, _exists=true }
 * context.observe()          → control_block_handle (watch → _weak=2)
 * coroutine finishes         → disown() (_strong=0, _weak=1, _exists=false)
 * handle destructs / cancel  → unwatch() (_weak=0) → delete block
 * @endcode
 */
#ifndef ACE_CONTROL_H
#define ACE_CONTROL_H

#include <atomic>
#include <cstddef>

#include "conduction.h"
#include "ace/common/terms.h"

namespace ace::coroutines {

    /**
     * @brief Intrusive reference-counted control block for a coroutine promise.
     *
     * @details Allocated immediately before the promise in memory by
     * `promise_traits::operator new`.  Stores the reference counts and an
     * optional pointer to a `control_conductor_handle` that enables external
     * join / cancel operations.
     *
     * All static methods accept a raw `void*` pointing to **either** the
     * block itself **or** a promise address; `get_block_from_address` converts
     * the latter to the former.
     */
    struct control_block {

        uint64_t _weak_refcount {1};                          ///< Number of watchers (handles). Initial value: 1 (the block itself).
        uint64_t _strong_refcount {1};                        ///< Number of owners (always the coroutine frame). Initial value: 1.
        control_conductor_handle* _control_conductor { nullptr }; ///< Optional conductor for external join/cancel; set by `setup_control_block()`.
        alignas(ACE_BUS_SIZE) bool _exists {true};            ///< `false` once the coroutine has finished (`disown()` was called).

        control_block() = default;

        ~control_block() = default;

        /**
         * @brief Check whether both reference counts are zero.
         * @param v_block  Pointer to the control block.
         * @return `true` if both `_weak_refcount` and `_strong_refcount` are 0.
         */
        static bool is_untracked(void* v_block);

        /**
         * @brief Decrement the strong (owner) count and mark the block as dead.
         * @details Called from `promise_type::final_suspend()` when the
         * coroutine has finished executing.
         * @param v_block  Pointer to the control block.
         * @return `true` if the block became untracked and can be freed.
         */
        static bool disown(void* v_block);

        /**
         * @brief Increment the weak (watcher) count.
         * @details Called when a new `control_block_handle` is constructed.
         * @param v_block  Pointer to the control block.
         * @return `true` if the block became untracked after the operation
         *         (only possible if `_weak_refcount` was already 0).
         */
        static bool watch(void* v_block);

        /**
         * @brief Decrement the weak (watcher) count.
         * @details Called from `control_block_handle`'s destructor or `cancel()`.
         * @param v_block  Pointer to the control block.
         * @return `true` if the block became untracked and can be freed.
         */
        static bool unwatch(void* v_block);

        /**
         * @brief Check whether the coroutine that owns this block has finished.
         * @param address  Raw promise address (offset by `control_block_size`).
         * @return `true` if `_exists == false`.
         */
        static bool is_disowned(void* address);

        /**
         * @brief Convert a promise address to the `control_block*` that precedes it.
         * @param address  Raw promise address returned by `operator new`.
         * @return Pointer to the control block.
         */
        static control_block* get_block_from_address(void* address);

    };

    /// @brief Byte size of `control_block`.  Used as an allocation prefix offset.
    inline constexpr std::size_t control_block_size = sizeof(control_block);

    /**
     * @brief Concept that checks whether a promise type carries a `control_block*`.
     * @tparam promise_t  Promise type to inspect.
     */
    template <typename promise_t>
    concept is_controled_promise = requires (promise_t p) {
        { std::remove_reference_t<decltype(p._block)>{} } -> std::same_as<control_block*>;
    };

    /**
     * @brief Copyable external handle to a coroutine's control block.
     *
     * @details Provides safe `cancel()`, `done()`, and `forward()` operations
     * from outside the scheduler — for example from `ace::futures::async_handle`
     * or user code that calls `context::observe()`.
     *
     * Copies increment the weak reference count; destruction decrements it.
     * When the count reaches zero the control block is freed.
     *
     * @warning **Not thread-safe.**  Do not share a single handle across threads
     * without external synchronization.
     */
    class control_block_handle {

        control_block* _block { nullptr };

        void release() {
            if (control_block::unwatch(_block))
                delete _block;
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
            control_block::watch(_block);
        }

        /**
         * @brief Construct from a coroutine handle whose promise satisfies
         *        `is_controled_promise`.
         * @tparam promise_t  Promise type (must have `_block` member).
         * @param promise  Coroutine handle to observe.
         */
        template <is_controled_promise promise_t>
        explicit control_block_handle(const std::coroutine_handle<promise_t>& promise) {
            _block = promise.promise()._block;
            control_block::watch(_block);
        }

        /// @brief Destructor.  Decrements the weak reference count; may delete the block.
        ~control_block_handle() { release(); }

        /**
         * @brief Request cancellation of the associated coroutine.
         * @details Calls `control_conductor->cancel()`, then releases this handle.
         * No-op if the handle is idle or the coroutine has already finished.
         */
        void cancel() {
            if (is_idle() or not _block->_control_conductor or done()) [[unlikely]]
                return;
            _block->_control_conductor->cancel();
            release();
        }

        /// @brief `true` if this handle does not reference any control block.
        [[nodiscard]] bool is_idle() const { return not _block; }

        /**
         * @brief `true` if the associated coroutine has finished.
         * @return `false` if `is_idle()`, otherwise `!_exists`.
         */
        [[nodiscard]] bool done() const {
            if (is_idle()) [[unlikely]] return false;
            return not _block->_exists;
        }

        /**
         * @brief Register a waiter context to be notified when the coroutine finishes.
         * @param waiter  Pointer to the `ace::async<>` context to register.
         * @return `true` if the waiter was accepted by the conductor.
         */
        bool forward(void* waiter) const {
            if (not _block) [[unlikely]] return false;
            if (done() or not _block->_control_conductor or waiter == nullptr) [[unlikely]]
                return false;
            return _block->_control_conductor->forward(waiter);
        }
    };


    // NOTE: Checks if there are no watchers or owners at the control block
    inline bool control_block::is_untracked(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        return  block->_weak_refcount == 0
            and block->_strong_refcount == 0;
    }

    // NOTE: Detaches owner from the control block
    inline bool control_block::disown(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        if (not block->_exists) [[unlikely]] goto end;
        --block->_strong_refcount;
        --block->_weak_refcount;
        block->_exists = false;
        end: return is_untracked(block);
    }

    // NOTE: Attaches spectator (not owner) to the control block
    inline bool control_block::watch(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        if (block->_weak_refcount == 0) [[unlikely]] goto end;
        ++block->_weak_refcount;
        end: return is_untracked(block);
    }

    // NOTE: Detaches spectator (not owner) from the control block
    inline bool control_block::unwatch(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        --block->_weak_refcount;
        return is_untracked(block);
    }

    // NOTE: Gets control block of passed promise address, and checks ownership status
    inline bool control_block::is_disowned(void* address) {
        return not get_block_from_address(address)->_exists;
    }

    // NOTE: Gets control block pointer from the raw promise address
    inline control_block* control_block::get_block_from_address(void* address) {
        return reinterpret_cast<control_block*>(static_cast<uint8_t*>(address) - control_block_size);
    }

} // end namespace ace::coroutines

#endif //ACE_CONTROL_H
