/**
 * @file
 * @details The file contains a service class definition.
 * Service is a special object with task to poll it in dispatcher service pool.
 */
#ifndef ACE_SERVICE_H
#define ACE_SERVICE_H

#include "dispatcher.h"
#include "ace/core/signal.h"
#include "ace/coroutines/context.h"

namespace ace::core {

    template <typename derived_t>
    struct service_traits {

        service_traits() = default;

        async<> service(sig_pipe_t& sig_pipe) {
            std::unique_ptr<signal_handler> sig { nullptr };
            while (true) {
                co_await static_cast <derived_t*>(this)->service_yank();
                if (sig_pipe.pop(sig)) [[unlikely]] {
                    switch (co_await sig->action()) {
                        case e_break: co_await std::suspend_always{};
                        case e_shutdown: co_return;
                        case e_idle:;
                        default:;
                    }
                    sig.reset();
                }
            }
        }
    };

    template <typename derived_t>
    class global_service_traits {

        global_service_traits() {
            dispatcher.spawn(service(dispatcher_sig_pipe));
        };

    friend derived_t;
    public:

        static derived_t& get_instance()
        {
            static derived_t instance;
            return instance;
        }

        async<> service(sig_pipe_t& sig_pipe) {
            std::unique_ptr<signal_handler> sig { nullptr };
            while (true) {
                co_await static_cast <derived_t*>(this)->service_yank();
                if (sig_pipe.pop(sig)) [[unlikely]] {
                    switch (co_await sig->action()) {
                        case e_break: co_await std::suspend_always{};
                        case e_shutdown: co_return;
                        case e_idle:;
                        default:;
                    }
                    sig.reset();
                }
                co_await std::suspend_always{};
            }
        }
    };

    template <typename service_t>
    concept is_service = requires(service_t s) {
        s.service_yank();
    };
}

#endif //ACE_SERVICE_H
