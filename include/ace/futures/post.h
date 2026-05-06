/**
 * @file post.h
 * @brief Awaitable command that posts a parallel task in the front of the current runner.
 *
 * @details @c ace::futures::post is used via @c co_await ace::post(task).
 * It differs from @c ace::schedule() in two ways:
 *
 *  1. <b>Same runner</b> — the new task is attached to the *same* runner as the
 *     calling coroutine (no cross-runner transfer).
 *  2. <b>No roaming</b> — both the poster and the posted task have @c _roaming
 *     set to @c false, preventing the balancer from migrating either.
 *
 * The @c await_suspend() returns @c false so the calling coroutine continues
 * executing immediately after the post — there is no suspension.
 *
 * @par Example
 * @code{.cpp}
 * auto handle = co_await ace::post(child());
 * // parent continues here; child runs concurrently on the same runner
 * co_await handle.join();
 * @endcode
 *
 * @see ace::futures::async_handle, ace::schedule
 */
#ifndef ACE_FUTURE_POST_H
#define ACE_FUTURE_POST_H

#include <ace/core/traits/future.h>
#include <ace/core/runner.h>
#include <ace/core/async_handle.h>

namespace ace::futures {

    /**
     * @brief Awaitable that attaches a new task to the current runner.
     *
     * @details Constructed by @c ace::post() and consumed by @c co_await.
     * Non-copyable, non-default-constructible.
     */
    class ACE_AWAIT_NODISCARD post final : public core::traits::future_traits<post> {

        task _task {};                       ///< The task to be posted.
        core::control_block_handle _handle;  ///< Control block handle obtained before attaching.

    public:

        IMPORT_FUTURE_ENV(post)

        post() = delete;
        post(const post&) = delete;
        post& operator=(const post&) = delete;

        /**
         * @brief Construct and immediately call @c observe() on the task.
         * @details @c observe() must be called *before* attaching the task to
         * the runner because the runner may execute and finish the task before
         * @c await_resume() is called.
         * @param new_task  The task to post.
         */
        [[nodiscard]] explicit post(task&& new_task)
            : _task(std::move(new_task))
            , _handle(_task.observe()) {}

        /**
         * @brief C++20 awaitable protocol — attach the task without suspending.
         * @details Disables roaming on both tasks, attaches the child task to
         * the current runner, and returns @c false so the calling coroutine
         * is not suspended.
         * @param coroutine  Handle to the calling coroutine's promise.
         * @return Always @c false — the caller is never suspended.
         */
        bool await_suspend(auto coroutine) {
            auto* runner_ptr = core::pool_to_runner(coroutine.promise()._runner_pool);
            _task._coroutine.promise()._roaming = coroutine.promise()._roaming = false;
            runner_ptr->attach_front(std::forward<task>(_task));
            return false;
        }

        /**
         * @brief C++20 awaitable protocol — return the task handle.
         * @return An @c async_handle wrapping the posted task's control block.
         */
        core::async_handle await_resume() const { return core::async_handle{_handle}; }

    };

}

#endif // ACE_FUTURE_POST_H
