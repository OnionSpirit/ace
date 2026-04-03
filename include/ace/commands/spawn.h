/**
 * @file spawn.h
 * @brief Awaitable command that spawns a parallel task in the current runner.
 *
 * @details `ace::commands::spawn` is used via `co_await ace::spawn(task)`.
 * It differs from `ace::schedule()` in two ways:
 *
 *  1. **Same runner** — the new task is attached to the *same* runner as the
 *     calling coroutine (no cross-runner transfer).
 *  2. **No roaming** — both the spawner and the spawned task have `_roaming`
 *     set to `false`, preventing the balancer from migrating either.
 *
 * The `await_suspend()` returns `false` so the calling coroutine continues
 * executing immediately after the spawn — there is no suspension.
 *
 * @par Example
 * @code{.cpp}
 * auto handle = co_await ace::spawn(child());
 * // parent continues here; child runs concurrently on the same runner
 * co_await handle.join();
 * @endcode
 *
 * @see ace::futures::async_handle, ace::schedule
 */
#ifndef ACE_COMMANDS_SPAWN_H
#define ACE_COMMANDS_SPAWN_H

#include "ace/futures/future.h"
#include "ace/core/runner.h"
#include "ace/futures/async_handle.h"

namespace ace::commands {

    /**
     * @brief Awaitable that attaches a new task to the current runner.
     *
     * @details Constructed by `ace::spawn()` and consumed by `co_await`.
     * Non-copyable, non-default-constructible.
     */
    class spawn final : public futures::future_traits<spawn> {

        async<> _task {};                          ///< The task to be spawned.
        coroutines::control_block_handle _handle;  ///< Control block handle obtained before attaching.

    public:

        IMPORT_FUTURE_ENV(spawn)

        spawn() = delete;
        spawn(const spawn&) = delete;
        spawn& operator=(const spawn&) = delete;

        /**
         * @brief Construct and immediately call `observe()` on the task.
         * @details `observe()` must be called *before* attaching the task to
         * the runner because the runner may execute and finish the task before
         * `await_resume()` is called.
         * @param new_task  The task to spawn.
         */
        [[nodiscard]] explicit spawn(async<>&& new_task)
            : _task(std::move(new_task))
            , _handle(_task.observe()) {}

        /**
         * @brief C++20 awaitable protocol — attach the task without suspending.
         * @details Disables roaming on both tasks, attaches the child task to
         * the current runner, and returns `false` so the calling coroutine
         * is not suspended.
         * @param coroutine  Handle to the calling coroutine's promise.
         * @return Always `false` — the caller is never suspended.
         */
        bool await_suspend(auto coroutine) {
            const auto* runner_ptr = core::pool_to_runner(coroutine.promise()._runner_pool);
            _task._coroutine.promise()._roaming = coroutine.promise()._roaming = false;
            runner_ptr->attach(std::forward<async<>>(_task));
            return false;
        }

        /**
         * @brief C++20 awaitable protocol — return the task handle.
         * @return An `async_handle` wrapping the spawned task's control block.
         */
        futures::async_handle await_resume() const { return futures::async_handle{_handle}; }

    };

}

#endif // ACE_COMMANDS_SPAWN_H
