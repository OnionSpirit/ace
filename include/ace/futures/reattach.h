#ifndef ACE_FUTURE_REATTACH_H
#define ACE_FUTURE_REATTACH_H

#include <ace/core/traits/future.h>
#include <ace/core/runner.h>

namespace ace::futures {

    class ACE_AWAIT_NODISCARD reattach : public core::traits::future_traits<reattach> {

        core::runner* _new_runner {};

        struct reattach_conductor;
        friend struct reattach_conductor;

    public:

        IMPORT_FUTURE_ENV(reattach)

        reattach() = delete;
        reattach(const reattach&) = delete;
        reattach& operator=(const reattach&) = delete;

        explicit reattach(runner_pool_t* new_pool)
            : _new_runner(core::runner::pool_to_runner(new_pool)) {}

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
        target_runner->threadsafe_attach(std::forward<task>(ctx));
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
