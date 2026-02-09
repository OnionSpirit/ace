#ifndef ACE_COMMANDS_SPAWN_H
#define ACE_COMMANDS_SPAWN_H

#include "command.h"
#include "ace/core/runner.h"

namespace ace::commands {

    class spawn : public command_traits<spawn> {

        async<> _task {};

    public:

        DECLARE_COMMAND(spawn)
        IMPORT_COMMAND_ENV

        spawn() = delete;
        spawn(const spawn&) = delete;
        spawn& operator=(const spawn&) = delete;

        explicit spawn(async<>&& new_task) {
            _task = std::move(new_task);
        }

        bool await_suspend(auto coroutine) {
            const auto* runner_ptr = core::pool_to_runner(coroutine.promise()._runner_pool);
            _task._coroutine.promise()._roaming = coroutine.promise()._roaming = false;
            runner_ptr->attach(std::forward<async<>>(_task));
            return false;
        }

        // TODO: Make return type as 'join_handler' future type, when I will write it
        static void await_resume() { }

    };

}

#endif // ACE_COMMANDS_SPAWN_H
