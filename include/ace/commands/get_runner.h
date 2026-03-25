#ifndef ACE_COMMANDS_GET_RUNNER_H
#define ACE_COMMANDS_GET_RUNNER_H

#include "ace/futures/future.h"

namespace ace::commands {

    struct get_runner : futures::future_traits<get_runner> {

        core::runner* _ptr {};

        IMPORT_FUTURE_ENV(get_runner)

        bool await_suspend(auto coroutine) {
            _ptr = core::pool_to_runner(coroutine.promise()._runner_pool);
            return false;
        }

        [[nodiscard]] core::runner* await_resume() const {
            return _ptr;
        }
    };

} // end namespace ace::commands

#endif // ACE_COMMANDS_GET_RUNNER_H
