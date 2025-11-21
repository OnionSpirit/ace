/**
 * @file
 * @details The file contains a signal class definition. Signal is a special object for service units control.
 */
#ifndef ACE_SIGNAL_H
#define ACE_SIGNAL_H

#include "ace/coroutines/context.h"

namespace ace::core {

    enum signal_trivial_orders {
        e_shutdown,
        e_idle,
        e_break
    };

    // NOTE: Special orders for services
    struct signal_handler {

        signal_handler() = default;

        virtual async<signal_trivial_orders> action() = 0;

        virtual ~signal_handler() = default;
    };

    typedef nukes::dynamic::mpsc_queue<std::unique_ptr<signal_handler>> sig_pipe_t;

}

#endif //ACE_SIGNAL_H
