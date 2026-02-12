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

    struct termination_signal : signal_handler {
        termination_signal() = default;
        ~termination_signal() override = default;
        async<signal_trivial_orders> action() override { co_return e_shutdown; }
    };

    struct interruption_signal : signal_handler {
        interruption_signal() = default;
        ~interruption_signal() override = default;
        async<signal_trivial_orders> action() override { co_return e_break; }
    };

    template <typename signal_t>
    std::unique_ptr<signal_handler> make_signal(signal_t sig) {
        return std::unique_ptr<signal_handler>(new signal_t(sig));
    }

}

#endif //ACE_SIGNAL_H
