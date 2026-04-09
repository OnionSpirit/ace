/**
 * @file conduction.h
 * @brief Conductor interfaces and the @c conductor_slot in-place storage.
 *
 * @details The <b>conductor pattern</b> is the primary mechanism by which ACE
 * decouples task forwarding from the runner's internal queue.
 *
 * ### How it works
 *
 * 1. A coroutine calls @c co_await some_future.
 * 2. The future's @c await_suspend() places a concrete conductor object into
 *    the @c conductor_slot stored in the coroutine's @c promise_type.
 * 3. The runner calls @c context::awake().  After resuming, it checks whether
 *    @c _runner_conductor is set.  If so, it calls
 *    @c conductor->forward(std::move(context)) instead of re-queuing.
 * 4. The conductor (owned by the future) enqueues the context in the future's
 *    own waiting structure (e.g., a channel waiters queue or a time-wheel slot).
 * 5. When the future becomes ready, it calls @c runner::reattach(context) to
 *    return the context to its original runner.
 *
 * ### Two conductor families
 *
 * | Interface | Purpose |
 * |---|---|
 * | @c runner_conductor_handle<C> | Move a task into a future's storage and optionally cancel it. |
 * | @c control_conductor_handle   | Provide join / cancel access to a promise's control block. |
 *
 * Both are stored in-place inside @c conductor_slot to avoid heap indirection.
 */
#ifndef ACE_CONDUCTOR_H
#define ACE_CONDUCTOR_H

#include "ace/common/terms.h"

namespace ace::coroutines {

    /**
     * @brief Abstract interface for conductors that forward coroutine contexts
     *        from the runner into a future's waiting structure.
     *
     * @details Derived conductors are created by futures (e.g., @c channel,
     * @c timeout, @c cutex) and stored inside the coroutine's @c conductor_slot.
     * The runner calls @c forward() after detecting the slot is occupied.
     *
     * @tparam runner_context_t  The coroutine context type being forwarded
     *                           (typically @c ace::task).
     */
    template <typename runner_context_t>
    struct runner_conductor_handle {

        runner_conductor_handle() noexcept = default;

        runner_conductor_handle(const runner_conductor_handle&) noexcept = default;

        runner_conductor_handle(runner_conductor_handle&&) noexcept = default;

        /**
         * @brief Transfer the coroutine context into the future's storage.
         * @param context  The suspended coroutine context to enqueue.
         */
        virtual void forward(runner_context_t&& context) = 0;

        /**
         * @brief Cancel the pending operation and wake all associated waiters.
         * @details The conductor marks the context as @c e_detached; the runner
         * will drop it on the next @c yank() call.
         */
        virtual void cancel() = 0;

        virtual ~runner_conductor_handle() = default;
    };

    /**
     * @brief Abstract interface for conductors that manage a coroutine's
     *        external control block (join / cancel from outside the scheduler).
     *
     * @details This conductor is installed into a @c control_block by
     * @c context::setup_control_block() and accessed through
     * @c control_block_handle.  It allows external code to:
     *  - @c forward(waiter) — register a waiter that will be resumed when the
     *    producer coroutine finishes.
     *  - @c cancel() — request cancellation of the producer coroutine.
     */
    struct control_conductor_handle {

        control_conductor_handle() noexcept = default;

        /**
         * @brief Register an external waiter that will be notified on finish.
         * @param waiter  Pointer to the @c ace::task context to notify.
         * @return @c true if the waiter was successfully registered.
         */
        virtual bool forward(void* waiter) noexcept = 0;

        /**
         * @brief Cancel the associated coroutine.
         */
        virtual void cancel() noexcept = 0;

        virtual ~control_conductor_handle() = default;
    };

    /**
     * @brief In-place storage that holds exactly one conductor object.
     *
     * @details @c conductor_slot avoids a heap allocation by storing the
     * conductor in a fixed-size aligned byte array (@c _area).  A raw pointer
     * @c _conductor is used as a discriminant (null ↔ empty).
     *
     * The slot supports three operations:
     *  - @b Assignment (@c operator=) — placement-new a new conductor,
     *    destroying the previous one if present.
     *  - @b Move-steal (@c operator<<) — transfer ownership from another slot
     *    without destroying.  Used to propagate conductors up a call stack.
     *  - @b Release (@c release()) — explicitly destroy and nullify.
     *
     * @tparam conductor_handle_t  The abstract base type of stored conductors.
     * @tparam slot_memsize_v      Maximum byte size of a concrete conductor
     *                             object.  Defaults to @c ACE_CONDUCTOR_MEM_SIZE.
     *
     * @warning All concrete conductors stored in this slot @b must fit within
     * @c slot_memsize_v bytes.  A @c static_assert enforces this at compile time.
     */
    template <typename conductor_handle_t, std::size_t slot_memsize_v = ACE_CONDUCTOR_MEM_SIZE>
    struct conductor_slot {

        /**
         * @brief Copy-assign a concrete conductor into this slot.
         * @details Uses placement-new; previous conductor is NOT destroyed
         * before the new one is created (caller must ensure slot is empty or
         * call @c release() first).
         * @tparam conductor_t  Concrete conductor type (must derive from
         *                      @c conductor_handle_t).
         * @param conductor  Conductor to copy-construct in-place.
         * @return Reference to @c *this.
         */
        template <typename conductor_t>
        requires std::derived_from<conductor_t, conductor_handle_t>
        conductor_slot& operator =(const conductor_t& conductor) {
            static_assert(sizeof(conductor_t) <= slot_memsize_v,
            "[conductor_carry]: conductor size can't be larger than passed slot memsize");
            _conductor = new (_area) conductor_t(std::forward<const conductor_t&>(conductor));
            return *this;
        }

        /**
         * @brief Move-assign a concrete conductor into this slot.
         * @details Calls @c release() first to destroy any existing conductor,
         * then move-constructs the new one in-place.
         * @tparam conductor_t  Concrete conductor type (must derive from
         *                      @c conductor_handle_t).
         * @param conductor  Conductor to move-construct in-place.
         * @return Reference to @c *this.
         */
        template <typename conductor_t>
        requires std::derived_from<conductor_t, conductor_handle_t>
        conductor_slot& operator =(conductor_t&& conductor) {
            static_assert(sizeof(conductor_t) <= slot_memsize_v,
            "[conductor_carry]: conductor size can't be larger than passed slot memsize");
            release();
            _conductor = new (_area) conductor_t(std::forward<conductor_t&&>(conductor));
            return *this;
        }

        /**
         * @brief Steal the conductor pointer from another slot without
         *        invoking the destructor on the source.
         *
         * @details Used to propagate a conductor up the call stack when a
         * nested coroutine suspends — the outer coroutine's promise takes
         * ownership of the conductor so the runner can find it.
         *
         * @tparam carry_t  Any type with @c _conductor and @c _area members.
         * @param carry     Source slot to steal from.
         * @return Reference to @c *this.
         */
        template<typename carry_t>
        requires requires { carry_t::_conductor; carry_t::_area; }
        conductor_slot& operator <<(carry_t& carry) noexcept {
            if (carry._conductor) {
                _conductor = carry._conductor;
                carry._conductor = nullptr;
            }
            return *this;
        }

        /**
         * @brief Destroy the held conductor and set the pointer to null.
         * @details Calls the virtual destructor of the concrete conductor type.
         */
        void release() {
            if (_conductor) {
                _conductor->~conductor_handle_t();
                _conductor = nullptr;
            }
        }

        /**
         * @brief Null the conductor pointer without calling its destructor.
         * @details Use this when ownership has been transferred elsewhere
         * (e.g., via @c operator<<).
         */
        void reset() {
            if (_conductor)
                _conductor = nullptr;
        }

        /// @brief Access the held conductor.  Returns @c nullptr if empty.
        [[nodiscard]] conductor_handle_t* get() const { return _conductor; }

        /// @brief Arrow operator for direct method access on the conductor.
        conductor_handle_t* operator->() const { return get(); }

        /// @brief @c true if a conductor is currently held.
        explicit operator bool() const { return _conductor != nullptr; };

        ~conductor_slot() { release(); };

        conductor_handle_t* _conductor {nullptr};                        ///< Pointer into @c _area (discriminant).
        alignas(ACE_BUS_SIZE) uint8_t _area [slot_memsize_v] {};         ///< In-place storage for the conductor object.
    };

}

#endif //ACE_CONDUCTOR_H
