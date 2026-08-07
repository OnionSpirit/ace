/**
 * @file reattach.h
 * @brief Awaitable command that migrates the current coroutine to another runner.
 *
 * @details @c ace::futures::reattach is @c co_await-ed to transfer the calling
 * coroutine from its current runner to a specified target runner.  The transfer
 * occurs via a @c reattach_router: the runner forwards the task, and the
 * router calls @c target_runner->attach().
 *
 * Usage:
 * @code{.cpp}
 * ace::task migrate() {
 *     auto* target = co_await ace::futures::get_runner{}; // get another runner
 *     co_await ace::futures::reattach{target};             // move to it
 *     co_return;
 * }
 * @endcode
 *
 * @see ace::futures::get_runner, ace::core::runner
 */
#ifndef ACE_FUTURE_REATTACH_H
#define ACE_FUTURE_REATTACH_H

#include <ace/core/traits/future.h>
#include <ace/core/runner.h>

namespace ace::futures {

    /**
     * @brief Awaitable that migrates the current coroutine to a target runner.
     *
     * @details Constructed with either a @c runner* or a @c omni_runner
     * (runner pool pointer).  When @c co_await-ed, installs a
     * @c reattach_router into the promise so the current runner forwards
     * the task to the target runner's queue.
     *
     * @note If the target runner is @c nullptr, @c await_ready() returns
     * @c true (no-op).
     */
    class ACE_AWAIT_NODISCARD reattach : public core::traits::future_traits<reattach> {

        core::runner* _new_runner {}; ///< Target runner to migrate to.

        struct reattach_router;
        friend struct reattach_router;

    public:

        IMPORT_FUTURE_ENV(reattach)

        /// @brief Default construction is forbidden — a target runner is required.
        reattach() = delete;
        /// @brief Copying a reattach command is forbidden.
        reattach(const reattach&) = delete;
        /// @brief Copy assignment is forbidden.
        reattach& operator=(const reattach&) = delete;

        /**
         * @brief Constructs the command from a runner pool pointer.
         * @param new_pool Target runner pool.
         */
        explicit reattach(omni_runner new_pool)
            : _new_runner(new_pool) {}

        /**
         * @brief Constructs the command from a runner pointer.
         * @param new_runner Target runner.
         */
        explicit reattach(core::runner* new_runner)
            : _new_runner(new_runner) {}

        /**
         * @brief @c true when there is no target runner (no-op).
         */
        bool await_ready() override { return _new_runner == nullptr; }

        /**
         * @brief Installs the migration router unless already on the target runner.
         * @param coroutine Caller coroutine promise accessor.
         * @return @c false when already on the target runner, @c true otherwise.
         */
        bool await_suspend(auto coroutine);

        /// @brief No value produced.
        void await_resume() { }

    };

}


//==============================- DEFINITIONS -==================================


#define ACE_FUTURE_REATTACH_SPACE \
ace::futures::reattach::

#define ACE_FUTURE_REATTACH_MEMBER(rtype) \
rtype ACE_FUTURE_REATTACH_SPACE


/**
 * @brief Router that migrates a task to another runner.
 *
 * @details On @c redirect() the task's runner is re-pointed to the target
 * and the node is returned to the target runner's queue.
 */
struct ACE_FUTURE_REATTACH_SPACE reattach_router : runner_router {

    /// @brief Default construction is forbidden.
    reattach_router() = delete;

    /**
     * @brief Binds the router to the migration target.
     * @param rnr Target runner.
     */
    explicit reattach_router(core::runner* rnr)
        : target_runner(rnr) {};

    /**
     * @brief Re-points the task's runner and returns it to the target queue.
     * @param node Task node being migrated.
     */
    void redirect(omni_node node) override {
        node->_data._coroutine.promise()._runner = target_runner;
        core::runner::reattach(node);
    }

    ~reattach_router() override = default;

    core::runner* target_runner {}; ///< Runner the task is migrated to.
};

ACE_FUTURE_REATTACH_MEMBER(bool)
/**
 * @brief Installs the migration router unless already on the target runner.
 * @param coroutine Caller coroutine promise accessor.
 * @return @c false when already on the target runner, @c true otherwise.
 */
await_suspend(auto coroutine) {
    // NOTE: Do not suspend if current and requested runners is same
    if (_new_runner == coroutine.promise()._runner.template as<core::runner>())
        return false;
    coroutine.promise()._runner_router = reattach_router{_new_runner};
    return true;
}

#undef ACE_FUTURE_REATTACH_SPACE
#undef ACE_FUTURE_REATTACH_MEMBER
#endif // ACE_FUTURE_REATTACH_H
