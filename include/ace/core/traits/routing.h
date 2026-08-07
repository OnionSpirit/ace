/**
 * @file routing.h
 * @brief Router interfaces and the @c router_slot in-place storage.
 *
 * @details The <b>router pattern</b> is the primary mechanism by which ACE
 * decouples task forwarding from the runner's internal queue.
 *
 * ### How it works
 *
 * 1. A coroutine calls @c co_await some_future.
 * 2. The future's @c await_suspend() places a concrete router object into
 *    the @c router_slot stored in the coroutine's @c promise_type.
 * 3. The runner calls @c async::awake().  After resuming, it checks whether
 *    @c _runner_router is set.  If so, it calls
 *    @c router->forward(std::move(async)) instead of re-queuing.
 * 4. The router (owned by the future) enqueues the async in the future's
 *    own waiting structure (e.g., a channel waiters queue or a time-wheel slot).
 * 5. When the future becomes ready, it calls @c runner::reattach(async) to
 *    return the async to its original runner.
 *
 * ### Two router families
 *
 * | Interface | Purpose |
 * |---|---|
 * | @c runner_router_handle<C> | Move a task into a future's storage and optionally cancel it. |
 * | @c control_router_handle   | Provide join / cancel access to a promise's control block. |
 *
 * Both are stored in-place inside @c router_slot to avoid heap indirection.
 */
#ifndef ACE_ROUTER_H
#define ACE_ROUTER_H

#include "memory.h"
#include "ace/core/tools/macro.h"

namespace ace::core::traits {

    /**
     * @brief Abstract interface for routers that forward coroutine asyncs
     *        from the runner into a future's waiting structure.
     *
     * @details Derived routers are created by futures (e.g., @c channel,
     * @c timeout, @c cutex) and stored inside the coroutine's @c router_slot.
     * The runner calls @c redirect() after detecting the slot is occupied.
     *
     * @tparam forwarded_node_t The coroutine async type being forwarded
     *                           (typically @c ace::task).
     */
    template <typename forwarded_node_t>
    struct runner_router_handle {

        /** @brief Default constructor. */
        runner_router_handle() noexcept = default;

        /** @brief Copy constructor. */
        runner_router_handle(const runner_router_handle&) noexcept = default;

        /** @brief Move constructor. */
        runner_router_handle(runner_router_handle&&) noexcept = default;

        /**
         * @brief Transfer the coroutine async into the future's storage.
         * @param node Queue node for the suspended coroutine async to enqueue.
         */
        virtual void redirect(forwarded_node_t node) {
            throw std::logic_error("runner_router_handle::redirect(...) - called but not overridden");
        };

        /**
         * @brief Cancel the pending operation.
         * @details The default implementation is a no-op; derived routers
         * override it to cancel the pending operation and wake all associated
         * waiters.
         */
        virtual void cancel() {};

        /** @brief Default destructor. */
        virtual ~runner_router_handle() = default;
    };

    /**
     * @brief Abstract interface for routers that manage a coroutine's
     *        external control block (join / cancel from outside the scheduler).
     *
     * @details This router is installed into a @c control_block by
     * @c async::setup_control_block() and accessed through
     * @c control_block_handle.  It allows external code to:
     *  - @c redirect(waiter) — register a waiter that will be resumed when the
     *    producer coroutine finishes.
     *  - @c cancel() — request cancellation of the producer coroutine.
     */
    struct async_router_handle {

        /** @brief Default constructor. */
        async_router_handle() noexcept = default;

        /**
         * @brief Register an external waiter that will be notified on finish.
         * @param waiter  Pointer to the @c ace::task async to notify.
         * @return @c true if the waiter was successfully registered.
         */
        virtual bool redirect(void* waiter) noexcept = 0;

        /**
         * @brief Cancel the associated coroutine.
         */
        virtual void cancel() noexcept = 0;

        /**
         * @brief Taking return value from a task
         * @return @c false if it is not automaton or task doesn't finished
         */
        virtual bool return_value(void*) noexcept = 0;

        /**
         * @brief Taking a yielded value from automaton coroutine
         * @details Copies @c _return_value to @c mem_ptr and sets status to @c e_executed
         * @param mem_ptr  Pointer to storage for the yielded value
         * @return @c true if value was captured, @c false otherwise
         */
        virtual bool yield_value(void*) noexcept { return false; }

        /**
         * @brief Check if coroutine has a yielded value ready
         * @return @c true if status is e_executed_with_value and not done
         */
        virtual bool has_yield() noexcept { return false; }

        /**
         * @brief Register a waiter to be resumed on the next yielded value.
         * @param node_ptr  Pointer to the @c omni_node waiter to register.
         * @return @c true if the waiter was registered, @c false otherwise.
         */
        virtual bool set_yield_waiter(void* /* omni_node */) noexcept { return false; }

        /**
         * @brief Cancel a previously registered yield waiter.
         * @return @c true if the yield waiter was canceled, @c false otherwise.
         */
        virtual bool cancel_yield() noexcept { return false; }

        /**
         * @brief manual destroy of the stack frame
         */
        virtual void destroy() noexcept = 0;

        /** @brief Default destructor. */
        virtual ~async_router_handle() = default;
    };

    /**
     * @brief In-place storage that holds exactly one router object.
     *
     * @details @c router_slot avoids a heap allocation by storing the
     * router in a fixed-size aligned byte array (@c _area).  A raw pointer
     * @c _router is used as a discriminant (null ↔ empty).
     *
     * The slot supports three operations:
     *  - @b Assignment (@c operator=) — placement-new a new router,
     *    destroying the previous one if present.
     *  - @b Move-steal (@c operator<<) — transfer ownership from another slot
     *    without destroying.  Used to propagate routers up a call stack.
     *  - @b Release (@c release()) — explicitly destroy and nullify.
     *
     * @tparam router_handle_t  The abstract base type of stored routers.
     * @tparam slot_memsize_v      Maximum byte size of a concrete router
     *                             object.  Defaults to @c ACE_ROUTER_MEM_SIZE.
     *
     * @warning All concrete routers stored in this slot @b must fit within
     * @c slot_memsize_v bytes.  A @c static_assert enforces this at compile time.
     */
    template <typename router_handle_t, std::size_t slot_memsize_v = ACE_ROUTER_MEM_SIZE>
    struct router_slot {

        /**
         * @brief Copy-assign a concrete router into this slot.
         * @details Uses placement-new; previous router is NOT destroyed
         * before the new one is created (caller must ensure slot is empty or
         * call @c release() first).
         * @tparam router_t  Concrete router type (must derive from
         *                      @c router_handle_t).
         * @param router  Router to copy-construct in-place.
         * @return Reference to @c *this.
         */
        template <typename router_t>
        requires std::derived_from<router_t, router_handle_t>
        router_slot& operator =(const router_t& router) {
            static_assert(sizeof(router_t) <= slot_memsize_v,
            "[router_carry]: router size can't be larger than passed slot memsize");
            _router = new (_area) router_t(std::forward<const router_t&>(router));
            return *this;
        }

        /**
         * @brief Move-assign a concrete router into this slot.
         * @details Calls @c release() first to destroy any existing router,
         * then move-constructs the new one in-place.
         * @tparam router_t  Concrete router type (must derive from
         *                      @c router_handle_t).
         * @param router  Router to move-construct in-place.
         * @return Reference to @c *this.
         */
        template <typename router_t>
        requires std::derived_from<router_t, router_handle_t>
        router_slot& operator =(router_t&& router) {
            static_assert(sizeof(router_t) <= slot_memsize_v,
            "[router_carry]: router size can't be larger than passed slot memsize");
            release();
            _router = new (_area) router_t(std::forward<router_t&&>(router));
            return *this;
        }

        /**
         * @brief Steal the router pointer from another slot without
         *        invoking the destructor on the source.
         *
         * @details Used to propagate a router up the call stack when a
         * nested coroutine suspends — the outer coroutine's promise takes
         * ownership of the router so the runner can find it.
         *
         * @tparam carry_t  Any type with @c _router and @c _area members.
         * @param carry     Source slot to steal from.
         * @return Reference to @c *this.
         */
        template<typename carry_t>
        requires requires { carry_t::_router; carry_t::_area; }
        router_slot& operator <<(carry_t& carry) noexcept {
            // TODO: Make only ptr copy without memcpy
            if (carry._router) {
                memcpy(_area, carry._area, slot_memsize_v);
                _router = reinterpret_cast<router_handle_t *>(_area);
                carry._router = nullptr;
            }
            return *this;
        }

        /**
         * @brief Destroy the held router and set the pointer to null.
         * @details Calls the virtual destructor of the concrete router type.
         */
        void release() {
            if (_router) {
                _router->~router_handle_t();
                _router = nullptr;
            }
        }

        /**
         * @brief Null the router pointer without calling its destructor.
         * @details Use this when ownership has been transferred elsewhere
         * (e.g., via @c operator<<).
         */
        void reset() {
            if (_router)
                _router = nullptr;
        }

        /// @brief Access the held router.  Returns @c nullptr if empty.
        [[nodiscard]] router_handle_t* get() const { return _router; }

        /// @brief Arrow operator for direct method access on the router.
        router_handle_t* operator->() const { return get(); }

        /// @brief @c true if a router is currently held.
        explicit operator bool() const { return _router != nullptr; };

        /** @brief Destructor: releases the held router. */
        ~router_slot() { release(); };

        router_handle_t* _router {nullptr};                        ///< Pointer into @c _area (discriminant).
        alignas(ACE_BUS_SIZE) uint8_t _area [slot_memsize_v] {};         ///< In-place storage for the router object.
    };

}

#endif //ACE_ROUTER_H
