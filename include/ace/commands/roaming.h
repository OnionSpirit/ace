/**
 * @file roaming.h
 * @brief Command that controls whether a coroutine may migrate between runners.
 *
 * @details When a task is submitted via `ace::schedule()`, the dispatcher sets
 * `_roaming = true`, allowing the balancer to migrate the task to any available
 * runner.  When spawned via `ace::spawn()`, roaming is disabled (`false`) so
 * the task stays on the same runner as its parent.
 *
 * This command lets the user toggle the flag mid-execution:
 *
 * @code{.cpp}
 * ace::async<> my_task() {
 *     // Pin this coroutine to its current runner (no migration)
 *     co_await ace::commands::roaming{false};
 *     // ...
 *     // Re-enable migration
 *     co_await ace::commands::roaming{true};
 *     co_return;
 * }
 * @endcode
 *
 * The command never actually suspends the coroutine (`await_suspend` returns
 * `false`) — it only mutates the promise flag.
 */
#ifndef ACE_COMMANDS_ROAMING_ROAMING_H
#define ACE_COMMANDS_ROAMING_ROAMING_H

#include "ace/futures/future.h"

namespace ace::commands {

    /**
     * @brief Awaitable command that sets the `_roaming` flag on the current promise.
     *
     * @details Non-suspending — `await_suspend()` returns `false` immediately.
     */
    class roaming : public futures::future_traits<roaming> {

        bool _is_roaming { true }; ///< Target roaming state.

    public:

        IMPORT_FUTURE_ENV(roaming)

        /// @brief Default: enable roaming.
        roaming() = default;

        /**
         * @brief Construct with an explicit roaming state.
         * @param is_roaming  `true` to allow cross-runner migration;
         *                    `false` to pin the task to its current runner.
         */
        explicit roaming(const bool is_roaming) : _is_roaming{is_roaming} {};

        roaming(const roaming&) = delete;
        roaming& operator=(const roaming&) = delete;

        /**
         * @brief Apply the roaming flag to the promise — never suspends.
         * @param coroutine  Handle to the calling coroutine's promise.
         * @return Always `false` — no suspension.
         */
        bool await_suspend(auto coroutine) {
            coroutine.promise()._roaming = _is_roaming;
            return false;
        }

        static void await_resume() noexcept {} ///< No value produced.

    };

}

#endif // ACE_COMMANDS_ROAMING_ROAMING_H
