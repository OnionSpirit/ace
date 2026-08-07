/**
 * @file roaming.h
 * @brief Command that controls whether a coroutine may migrate between runners.
 *
 * @details When a task is submitted via @c ace::schedule(), the dispatcher sets
 * @c _roaming = true, allowing the balancer to migrate the task to any available
 * runner.  When spawned via @c ace::spawn(), roaming is disabled (@c false) so
 * the task stays on the same runner as its parent.
 *
 * This command lets the user toggle the flag mid-execution:
 *
 * @code{.cpp}
 * ace::task my_task() {
 *     // Pin this coroutine to its current runner (no migration)
 *     co_await ace::futures::roaming{false};
 *     // ...
 *     // Re-enable migration
 *     co_await ace::futures::roaming{true};
 *     co_return;
 * }
 * @endcode
 *
 * The command never actually suspends the coroutine (@c await_suspend returns
 * @c false) — it only mutates the promise flag.
 */
#ifndef ACE_FUTURE_ROAMING_ROAMING_H
#define ACE_FUTURE_ROAMING_ROAMING_H

#include <ace/core/traits/future.h>

namespace ace::futures {

    /**
     * @brief Awaitable command that sets the @c _roaming flag on the current promise.
     *
     * @details Non-suspending — @c await_suspend() returns @c false immediately.
     * Captures the current runner and exposes it as the await result.
     */
    class ACE_AWAIT_NODISCARD roaming : public core::traits::future_traits<roaming> {

        omni_runner _rnr {};       ///< Current runner captured at await time.
        bool _is_roaming { true }; ///< Target roaming state.

    public:

        IMPORT_FUTURE_ENV(roaming)

        /// @brief Default: enable roaming.
        roaming() = default;

        /**
         * @brief Construct with an explicit roaming state.
         * @param is_roaming  @c true to allow cross-runner migration;
         *                    @c false to pin the task to its current runner.
         */
        explicit roaming(const bool is_roaming) : _is_roaming{is_roaming} {};

        /// @brief Copying a roaming command is forbidden.
        roaming(const roaming&) = delete;
        /// @brief Copy assignment is forbidden.
        roaming& operator=(const roaming&) = delete;

        /**
         * @brief Apply the roaming flag to the promise — never suspends.
         * @param coroutine  Handle to the calling coroutine's promise.
         * @return Always @c false — no suspension.
         */
        bool await_suspend(auto coroutine) {
            coroutine.promise()._roaming = _is_roaming;
            _rnr = coroutine.promise()._runner;
            return false;
        }

        /**
         * @brief Returns the runner the task was on when the command was applied.
         * @return The captured @c omni_runner.
         */
        auto await_resume() noexcept { return _rnr; }

    };

}

#endif // ACE_FUTURE_ROAMING_ROAMING_H
