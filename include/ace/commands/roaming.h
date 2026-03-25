#ifndef ACE_COMMANDS_ROAMING_ROAMING_H
#define ACE_COMMANDS_ROAMING_ROAMING_H

#include "ace/futures/future.h"

namespace ace::commands {

    class roaming : public futures::future_traits<roaming> {

        bool _is_roaming { true };

    public:

        IMPORT_FUTURE_ENV(roaming)

        roaming() = default;
        explicit roaming(const bool is_roaming) : _is_roaming{is_roaming} {};

        roaming(const roaming&) = delete;
        roaming& operator=(const roaming&) = delete;

        bool await_suspend(auto coroutine) {
            coroutine.promise()._roaming = _is_roaming;
            return false;
        }

        static void await_resume() noexcept {}

    };

}

#endif // ACE_COMMANDS_ROAMING_ROAMING_H
