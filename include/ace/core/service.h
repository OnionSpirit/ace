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

    template <typename service_t>
    concept is_service_faced = requires(service_t s) {
        { s.service_yank() } -> std::same_as<promise<bool>>;
    };

    template <typename derived_t>
    class service_traits {

        static void crtp_asserter() {
            static_assert(is_service_faced<derived_t>,
                "Derived type doesn't have 'service_yank()' function, "
                "or it's return type is not 'ace::promise<bool>'");
            static_assert(std::derived_from<derived_t, service_traits>,
                "Derived type is not actually derived from 'service_traits<DerivedT>'");
        };

        static thread_local bool _detached;
        friend derived_t;

        service_traits() { crtp_asserter(); respawn(); };

        void respawn(runner* rnr = nullptr) {
            dispatcher::get_instance().schedule(service(dispatcher::get_sig_pipe()), rnr);
            _detached = false;
        }

        async<> service(sig_pipe_t& sig_pipe) {
            std::unique_ptr<signal_handler> sig { nullptr };
            while (not _detached) {
                _detached = co_await static_cast<derived_t*>(this)->service_yank();
                if (sig_pipe.pop(sig)) [[unlikely]] {
                    const auto action_result = co_await sig->action();
                    sig_pipe.push(std::move(sig));
                    switch (action_result) {
                        case e_break: co_await std::suspend_always{};
                        case e_shutdown: co_return;
                        case e_idle: break;
                        default:;
                    }
                    sig.reset();
                }
                co_await std::suspend_always{};
            }
        }

    public:

        static derived_t& attach(runner_pool_t* rnr = nullptr) {
            static derived_t instance;
            if (instance._detached) instance.respawn(reinterpret_cast<runner*>(rnr));
            return instance;
        }

        static derived_t& get_instance() {
            static derived_t instance;
            return instance;
        }

    };

    template <typename derived_t>
    thread_local bool service_traits<derived_t>::_detached {true};

    template <typename service_t>
    concept is_service = is_service_faced<service_t>
        and std::derived_from<service_t, service_traits<service_t>>;
}

#endif //ACE_SERVICE_H
