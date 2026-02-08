#ifndef ACE_COMMANDS_GET_RUNNER_H
#define ACE_COMMANDS_GET_RUNNER_H

#include "command.h"
#include "ace/core/balancer.h"

namespace ace::commands {

    struct get_runner : command_traits<get_runner> {

        core::runner* _ptr {};

        DECLARE_COMMAND(get_runner)
        IMPORT_COMMAND_ENV

        bool await_suspend(auto coroutine) {
            _ptr = reinterpret_cast<core::runner*>(coroutine.promise()._runner_pool);
            return true;
        }

        core::runner* await_resume() const {
            return _ptr;
        }
    };

} // end namespace ace::commands

#endif // ACE_COMMANDS_GET_RUNNER_H
