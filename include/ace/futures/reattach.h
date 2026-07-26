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

        core::runner* _new_runner {};

        struct reattach_router;
        friend struct reattach_router;

    public:

        IMPORT_FUTURE_ENV(reattach)

        reattach() = delete;
        reattach(const reattach&) = delete;
        reattach& operator=(const reattach&) = delete;

        explicit reattach(omni_runner new_pool)
            : _new_runner(new_pool) {}

        explicit reattach(core::runner* new_runner)
            : _new_runner(new_runner) {}

        bool await_ready() override { return _new_runner == nullptr; }

        bool await_suspend(auto coroutine);

        void await_resume() { }

    };

}


//==============================- DEFINITIONS -==================================


#define ACE_FUTURE_REATTACH_SPACE \
ace::futures::reattach::

#define ACE_FUTURE_REATTACH_MEMBER(rtype) \
rtype ACE_FUTURE_REATTACH_SPACE


struct ACE_FUTURE_REATTACH_SPACE reattach_router : runner_router {

    reattach_router() = delete;

    explicit reattach_router(core::runner* rnr)
        : target_runner(rnr) {};

    void redirect(omni_node node) override {
        node->_data._coroutine.promise()._runner = target_runner;
        core::runner::reattach(node);
    }

    ~reattach_router() override = default;

    core::runner* target_runner {};
};

ACE_FUTURE_REATTACH_MEMBER(bool)
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
