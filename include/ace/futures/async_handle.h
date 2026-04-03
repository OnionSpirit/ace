/**
 * @file async_handle.h
 * @brief External handle to a spawned coroutine (`ace::futures::async_handle`)
 *        and the underlying join future (`ace::futures::join_handler`).
 *
 * @details `async_handle` is returned by `co_await ace::spawn(task)` and
 * allows the spawning coroutine to:
 *  - Query whether the child has finished (`done()`).
 *  - Wait for the child to finish (`co_await handle.join()`).
 *  - Cancel the child (`cancel()`).
 *
 * Internally it holds a `control_block_handle` that keeps the child's control
 * block alive via a weak reference even after the child has finished.
 *
 * ### Lifecycle
 *
 * @code
 * auto handle = co_await ace::spawn(child_task());
 * // handle.done() == false  (child is still running)
 * co_await handle.join();    // suspends parent until child finishes
 * // handle.done() == true
 * @endcode
 *
 * @see ace::commands::spawn, ace::coroutines::control_block_handle
 */
#ifndef ACE_FUTURE_ASYNC_HANDLE_H
#define ACE_FUTURE_ASYNC_HANDLE_H

#include "future.h"
#include "ace/coroutines/context.h"

namespace ace::futures {

    /**
     * @brief Awaitable future that suspends the caller until a target coroutine finishes.
     *
     * @details Used internally by `async_handle::join()`.  When `co_await`-ed,
     * a `join_handler_conductor` is placed in the caller's promise.  When the
     * target coroutine's destructor calls `release_waiters()`, the conductor's
     * `forward()` method enqueues the caller back into the target's waiters
     * queue, which is then drained — waking the caller.
     */
    class join_handler : public future_traits<join_handler> {

    protected:

        coroutines::control_block_handle _handle; ///< Weak reference to the target coroutine's control block.

        struct join_handler_conductor;

    public:

        IMPORT_FUTURE_ENV(join_handler)

        join_handler() = default;

        /**
         * @brief Construct from an existing control block handle.
         * @param handle  Handle to the target coroutine.
         */
        explicit join_handler(const coroutines::control_block_handle& handle)
            : _handle{handle} {}

        /**
         * @brief C++20 awaitable protocol — check if target already finished.
         * @return `true` if the handle is idle (null) or the target has finished.
         */
        bool await_ready() override {
            if (_handle.is_idle()) return true;
            return _handle.done();
        }

        /**
         * @brief C++20 awaitable protocol — register as a waiter.
         * @details Installs a `join_handler_conductor` that will forward this
         * context into the target's waiters queue when the target finishes.
         * @tparam promise_u  Promise type of the outer (waiting) coroutine.
         * @param outer       Handle to the outer coroutine.
         * @return Always `true` — always suspends if `await_ready()` returned `false`.
         */
        template<typename promise_u>
        bool await_suspend(std::coroutine_handle<promise_u> outer);

        /**
         * @brief C++20 awaitable protocol — return completion status.
         * @return `true` if the target coroutine has finished.
         */
        [[nodiscard]] bool await_resume() const { return _handle.done(); }
    };

    /**
     * @brief Public handle to a spawned coroutine.
     *
     * @details Returned by `co_await ace::spawn(task)`.  Provides join, done,
     * and cancel operations.
     *
     * @note `async_handle` is **not** default-constructible.  It must be
     * obtained via `co_await ace::spawn(...)`.
     */
    class async_handle final : protected join_handler {

    public:

        async_handle() = delete;

        /**
         * @brief Construct from a control block handle.
         * @details Called by `commands::spawn::await_resume()`.
         * @param handle  Handle to the spawned coroutine's control block.
         */
        explicit async_handle(const coroutines::control_block_handle& handle)
            : join_handler(handle) {}

        /**
         * @brief Return the underlying `join_handler` awaitable.
         * @details Call as `co_await handle.join()` to suspend the current
         * coroutine until the target finishes.
         * @return Reference to the `join_handler` base.
         */
        [[nodiscard]] auto join() noexcept -> join_handler&;

        /**
         * @brief Check if the target coroutine has finished.
         * @return `true` if done.
         */
        [[nodiscard]] bool done() const { return _handle.done(); }

        /**
         * @brief Cancel the target coroutine.
         * @details Sets the target status to `e_detached`.  The runner will
         * drop the coroutine on the next `yank()`.
         */
        void cancel() { _handle.cancel(); }

    };

    struct join_handler::join_handler_conductor final : conductor_handler_t {

        coroutines::control_block_handle _handle;

        join_handler_conductor() = delete;

        explicit join_handler_conductor(const coroutines::control_block_handle& handle) : _handle{handle} {}

        void forward(async<>&& ctx) override { _handle.forward(&ctx); }

        // TODO: Finish later
        void cancel() override {  }

        ~join_handler_conductor() override = default;

    };

} // end namespace ace::futures


//==============================- DEFINITIONS -==================================


#define ACE_FUTURE_ASYNC_HANDLE_SPACE \
ace::futures::async_handle::

#define ACE_FUTURE_ASYNC_HANDLE_MEMBER(returnT) \
returnT ACE_FUTURE_ASYNC_HANDLE_SPACE

#define ACE_FUTURE_JOIN_HANDLER_FUTURE_SPACE \
ace::futures::join_handler::

#define ACE_FUTURE_JOIN_HANDLER_FUTURE_MEMBER(returnT) \
returnT ACE_FUTURE_JOIN_HANDLER_FUTURE_SPACE


ACE_FUTURE_ASYNC_HANDLE_MEMBER(auto)
join() noexcept -> join_handler& { return *static_cast<join_handler*>(this); }

ACE_FUTURE_JOIN_HANDLER_FUTURE_MEMBER(template<typename promise_u> bool)
await_suspend(std::coroutine_handle<promise_u> outer) {
    outer.promise()._runner_conductor = join_handler_conductor{_handle};
    return true;
}


#undef ACE_FUTURE_ASYNC_HANDLE_SPACE
#undef ACE_FUTURE_ASYNC_HANDLE_MEMBER
#undef ACE_FUTURE_JOIN_HANDLER_FUTURE_SPACE
#undef ACE_FUTURE_JOIN_HANDLER_FUTURE_MEMBER

#endif //ACE_FUTURE_ASYNC_HANDLE_H
