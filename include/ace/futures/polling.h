/**
 * @file polling.h
 * @brief Awaitable command that sets the @c _polling flag on the current promise.
 *
 * @details A coroutine with @c _polling == true is treated as a low-priority
 * background task by the runner — it is moved to @c _vortex_pool instead of
 * the main task pool.  Used internally by vortex services and for user-level
 * polling coroutines.
 *
 * The command never suspends — @c await_suspend() returns @c false.
 *
 * @see ace::core::runner::yank(), ace::core::traits::vortex_traits
 */
#ifndef ACE_FUTURE_POLLING_H
#define ACE_FUTURE_POLLING_H

#include <ace/core/traits/future.h>

namespace ace::futures {

    /**
     * @brief Awaitable command that sets the @c _polling flag on the current promise.
     *
     * @details Non-suspending — @c await_suspend() returns @c false immediately.
     */
    class ACE_AWAIT_NODISCARD polling : public core::traits::future_traits<polling> {

        bool _is_polling { true }; ///< Target polling state.

    public:

        IMPORT_FUTURE_ENV(polling)

        /// @brief Default: enable polling.
        polling() = default;

        /**
         * @brief Construct with an explicit polling state.
         * @param is_polling  @c true to mark for special polling task processing;
         *                    @c false otherwise.
         */
        explicit polling(const bool is_polling) : _is_polling{is_polling} {};

        polling(const polling&) = delete;
        polling& operator=(const polling&) = delete;

        /**
         * @brief Apply the polling flag to the promise — never suspends.
         * @param coroutine  Handle to the calling coroutine's promise.
         * @return Always @c false — no suspension.
         */
        bool await_suspend(auto coroutine) {
            coroutine.promise()._polling = _is_polling;
            return false;
        }

        static void await_resume() noexcept {} ///< No value produced.

    };

}

#endif //ACE_FUTURE_POLLING_H
