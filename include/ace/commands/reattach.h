#ifndef ACE_COMMANDS_REATTACH_H
#define ACE_COMMANDS_REATTACH_H

#include "command.h"
#include "ace/core/runner.h"

namespace ace::commands {

    class reattach : public command_traits<reattach> {

        core::runner* _new_runner {};

        struct reattach_conductor;
        friend struct reattach_conductor;

    public:

        DECLARE_COMMAND(reattach)
        IMPORT_COMMAND_ENV

        reattach() = delete;
        reattach(const reattach&) = delete;
        reattach& operator=(const reattach&) = delete;

        explicit reattach(runner_pool_t* new_pool)
            : _new_runner(core::pool_to_runner(new_pool)) {}

        explicit reattach(core::runner* new_runner)
            : _new_runner(new_runner) {}

        bool await_ready() override { return _new_runner == nullptr; }

        bool await_suspend(auto coroutine);

        // TODO: Make return type as 'join_handler' future type, when I will write it
        static void await_resume() { }

    };

}


//==============================- DEFINITIONS -==================================


#define ACE_COMMANDS_REATTACH_SPACE \
ace::commands::reattach::

#define ACE_COMMANDS_REATTACH_MEMBER(rtype) \
rtype ACE_COMMANDS_REATTACH_SPACE


struct ACE_COMMANDS_REATTACH_SPACE reattach_conductor : conductor_handler_t {

    reattach_conductor() = delete;

    explicit reattach_conductor(core::runner* rnr)
        : target_runner(rnr) {};

    void forward(async<>&& ctx) override {
        target_runner->attach(std::forward<async<>>(ctx));
    }

    ~reattach_conductor() override = default;

    core::runner* target_runner {};
};

ACE_COMMANDS_REATTACH_MEMBER(bool)
await_suspend(auto coroutine) {
    coroutine.promise()._conductor = reattach_conductor{_new_runner};
    return true;
}

#undef ACE_COMMANDS_REATTACH_SPACE
#undef ACE_COMMANDS_REATTACH_MEMBER
#endif // ACE_COMMANDS_REATTACH_H
