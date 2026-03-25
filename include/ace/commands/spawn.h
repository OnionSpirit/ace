#ifndef ACE_COMMANDS_SPAWN_H
#define ACE_COMMANDS_SPAWN_H

#include "ace/futures/future.h"
#include "ace/core/runner.h"
#include "ace/futures/async_handle.h"

namespace ace::commands {

    class spawn final : public futures::future_traits<spawn> {

        async<> _task {};
        coroutines::control_block_handle _handle;

    public:

        IMPORT_FUTURE_ENV(spawn)

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

        futures::async_handle await_resume() const { return futures::async_handle{_handle}; }

    };

}

#endif // ACE_COMMANDS_SPAWN_H
