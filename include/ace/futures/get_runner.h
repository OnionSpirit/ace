/**
 * @file get_runner.h
 * @brief Command that retrieves a pointer to the current runner.
 *
 * @details Useful for explicitly targeting a specific runner when spawning
 * tasks or for diagnostic purposes.
 *
 * @code{.cpp}
 * ace::task my_task() {
 *     auto* runner = co_await ace::futures::get_runner{};
 *     // runner is the ace::core::runner that owns this coroutine
 *     ace::schedule(other_task(), runner);  // schedule on the same runner
 *     co_return;
 * }
 * @endcode
 *
 * The command never suspends (@c await_suspend returns @c false).
 */
#ifndef ACE_FUTURE_GET_RUNNER_H
#define ACE_FUTURE_GET_RUNNER_H

#include <ace/core/traits/future.h>

namespace ace::futures {

    /**
     * @brief Awaitable command that returns the calling coroutine's runner.
     *
     * @details Non-suspending — reads the @c _runner_pool pointer from the
     * promise and converts it to a @c runner* via @c pool_to_runner().
     */
    struct get_runner : core::traits::future_traits<get_runner> {

        core::runner* _ptr {}; ///< Pointer filled in by @c await_suspend.

        IMPORT_FUTURE_ENV(get_runner)

        /**
         * @brief Capture the current runner pointer from the promise.
         * @param coroutine  Handle to the calling coroutine's promise.
         * @return Always @c false — no suspension.
         */
        bool await_suspend(auto coroutine) {
            _ptr = core::pool_to_runner(coroutine.promise()._runner_pool);
            return false;
        }

        /**
         * @brief Return the captured runner pointer.
         * @return Pointer to the current @c ace::core::runner, or @c nullptr
         *         if the coroutine has no associated runner yet.
         */
        [[nodiscard]] core::runner* await_resume() const {
            return _ptr;
        }
    };

} // end namespace ace::futures

#endif // ACE_FUTURE_GET_RUNNER_H
