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
 * The command suspends the coroutine once (@c await_suspend returns @c true)
 * so the runner is captured while the task is parked in its queue.
 */
#ifndef ACE_FUTURE_GET_RUNNER_H
#define ACE_FUTURE_GET_RUNNER_H

#include <ace/core/traits/future.h>
#include <ace/core/runner.h>

namespace ace::futures {

    /**
     * @brief Awaitable command that returns the calling coroutine's runner.
     *
     * @details Suspending — reads the @c _runner pointer from the
     * promise and returns it as @c runner*.
     */
    struct ACE_AWAIT_NODISCARD get_runner : core::traits::future_traits<get_runner> {

        omni_runner _ptr {}; ///< Pointer filled in by @c await_suspend.

        IMPORT_FUTURE_ENV(get_runner)

        /**
         * @brief Capture the current runner pointer from the promise.
         * @param coroutine  Handle to the calling coroutine's promise.
         * @return Always @c false — no suspension.
         */
        bool await_suspend(auto coroutine) {
            _ptr = coroutine.promise()._runner;
            return true;
        }

        /**
         * @brief Return the captured runner pointer.
         * @return Pointer to the current @c ace::core::runner, or @c nullptr
         *         if the coroutine has no associated runner yet.
         */
        [[nodiscard]] core::runner* await_resume() {
            return _ptr;
        }
    };

} // end namespace ace::futures

// NOTE: The short alias ace::get_runner is only exposed when ace/ace.h
// (quick-start header) was included before this file — its ACE_H guard
// switches aliases on.
#ifdef ACE_H
namespace ace {
    /// @brief Short alias for @c ace::futures::get_runner.
    using get_runner = futures::get_runner;
}
#endif

#endif // ACE_FUTURE_GET_RUNNER_H
