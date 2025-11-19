#ifndef ACE_ALIASES_H
#define ACE_ALIASES_H

#include "ace/promises/async.h"

// NOTE: Common aliases
namespace ace {

    // NOTE: Type alias for lazy coroutines
    template<typename returnT =void>
    using promise = promises::async<returnT>;

    // NOTE: Type alias for lazy coroutines
    template<typename returnT =void>
    using lazy = promises::async<returnT, promises::differed>;

    // NOTE: Type alias for spawnable coroutines
    using task = lazy<>;

    // NOTE: Wrapper to spawn and manage coroutines in 'hubs' which is not 'task'
    task task_wrap(auto&& some_async) {
        co_await some_async;
        co_return;
    }

    // NOTE: Declaration of common hub_handler type
    typedef task::hub_t hub_handler_t;
}

#endif //ACE_ALIASES_H
