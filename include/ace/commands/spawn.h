#ifndef ACE_COMMANDS_SPAWN_H
#define ACE_COMMANDS_SPAWN_H

#include "command.h"
#include "ace/core/runner.h"

namespace ace::commands {

    class spawn : public command_traits<spawn> {

        async<> _task {};
        coroutines::control_block_handle _handle;

    public:

        DECLARE_COMMAND(spawn)
        IMPORT_COMMAND_ENV

        spawn() = delete;
        spawn(const spawn&) = delete;
        spawn& operator=(const spawn&) = delete;

        explicit spawn(async<>&& new_task)
            : _task(std::move(new_task))
            , _handle(_task.observe()) {}

        bool await_suspend(auto coroutine) {
            const auto* runner_ptr = core::pool_to_runner(coroutine.promise()._runner_pool);
            _task._coroutine.promise()._roaming = coroutine.promise()._roaming = false;
            runner_ptr->attach(std::forward<async<>>(_task));
            return false;
        }

        // TODO: Make return type as 'join_handler' future type, when I will write it
        coroutines::control_block_handle await_resume() { return _handle; }

    };

}

#endif // ACE_COMMANDS_SPAWN_H
