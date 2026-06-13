/**
 * @file signal.h
 * @brief Signal system for controlling vortex services at runtime.
 *
 * @details Defines @c signal_handler — an abstract awaitable that vortex
 * services inspect after each @c ping() cycle.  Two built-in signals:
 *
 *  - @c termination_signal — requests the vortex to shut down (@c e_shutdown).
 *  - @c interruption_signal — requests the vortex to suspend (@c e_break).
 *
 * Signals are delivered through a per-dispatcher @c sig_pipe_t (lock-free
 * MPSC queue).  The dispatcher exposes @c ace::interrupt() and
 * @c ace::terminate() to push signals, and @c ace::reset_signal() to drain
 * the pipe.
 *
 * @see ace::core::traits::vortex_traits, ace::core::dispatcher
 */
#ifndef ACE_SIGNAL_H
#define ACE_SIGNAL_H

#include "ace/core/async.h"

namespace ace::core {

    /**
     * @brief Signal actions that vortex services respond to.
     */
    enum signal_trivial_orders {
        e_shutdown, ///< Stop the vortex service permanently.
        e_idle,     ///< No action — continue normal operation.
        e_break     ///< Suspend the current vortex iteration.
    };

    /**
     * @brief Abstract signal handler — each signal type returns a specific
     *        order when action() is awaited.
     */
    struct signal_handler {

        signal_handler() = default;

        virtual async<signal_trivial_orders> action() = 0;

        virtual ~signal_handler() = default;
    };

    /// @brief Alias for the signal pipe type (lock-free MPSC queue).
    typedef nukes::dynamic::mpsc_queue<std::unique_ptr<signal_handler>> sig_pipe_t;

    /**
     * @brief Signal that requests vortex shutdown (@c e_shutdown).
     */
    struct termination_signal : signal_handler {
        termination_signal() = default;
        ~termination_signal() override = default;
        async<signal_trivial_orders> action() override { co_return e_shutdown; }
    };

    /**
     * @brief Signal that requests vortex suspension (@c e_break).
     */
    struct interruption_signal : signal_handler {
        interruption_signal() = default;
        ~interruption_signal() override = default;
        async<signal_trivial_orders> action() override { co_return e_break; }
    };

    /**
     * @brief Factory function for creating signals.
     * @tparam signal_t  Signal type (must derive from @c signal_handler).
     * @param sig  The signal to wrap.
     * @return A @c unique_ptr<signal_handler> owning a copy of @c sig.
     */
    template <typename signal_t>
    std::unique_ptr<signal_handler> make_signal(signal_t sig) {
        return std::unique_ptr<signal_handler>(new signal_t(sig));
    }

}

#endif //ACE_SIGNAL_H
