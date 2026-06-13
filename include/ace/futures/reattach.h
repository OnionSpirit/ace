/**
 * @file reattach.h
 * @brief Awaitable command that migrates the current coroutine to another runner.
 *
 * @details @c ace::futures::reattach is @c co_await-ed to transfer the calling
 * coroutine from its current runner to a specified target runner.  The transfer
 * occurs via a @c reattach_conductor: the runner forwards the task, and the
 * conductor calls @c target_runner->attach().
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
     * @details Constructed with either a @c runner* or a @c cast_ptr
     * (runner pool pointer).  When @c co_await-ed, installs a
     * @c reattach_conductor into the promise so the current runner forwards
     * the task to the target runner's queue.
     *
     * @note If the target runner is @c nullptr, @c await_ready() returns
     * @c true (no-op).
     */
    class ACE_AWAIT_NODISCARD reattach : public core::traits::future_traits<reattach> {

        core::runner* _new_runner {};

        struct reattach_conductor;
        friend struct reattach_conductor;

    public:

        IMPORT_FUTURE_ENV(reattach)

        reattach() = delete;
        reattach(const reattach&) = delete;
        reattach& operator=(const reattach&) = delete;

        explicit reattach(core::cast_ptr new_pool)
            : _new_runner(new_pool.as<core::runner>()) {}

        explicit reattach(core::runner* new_runner)
            : _new_runner(new_runner) {}

        bool await_ready() override { return _new_runner == nullptr; }

        bool await_suspend(auto coroutine);

        // TODO: Make return type as 'join_handler' future type, when I will write it
        static void await_resume() { }

    };

}


//==============================- DEFINITIONS -==================================


#define ACE_FUTURE_REATTACH_SPACE \
ace::futures::reattach::

#define ACE_FUTURE_REATTACH_MEMBER(rtype) \
rtype ACE_FUTURE_REATTACH_SPACE


struct ACE_FUTURE_REATTACH_SPACE reattach_conductor : conductor_handler_t {

    reattach_conductor() = delete;

    explicit reattach_conductor(core::runner* rnr)
        : target_runner(rnr) {};

    void forward(task&& ctx) override {
        target_runner->attach(std::forward<task>(ctx));
    }

    ~reattach_conductor() override = default;

    core::runner* target_runner {};
};

ACE_FUTURE_REATTACH_MEMBER(bool)
await_suspend(auto coroutine) {
    coroutine.promise()._conductor = reattach_conductor{_new_runner};
    return true;
}

#undef ACE_FUTURE_REATTACH_SPACE
#undef ACE_FUTURE_REATTACH_MEMBER
#endif // ACE_FUTURE_REATTACH_H
