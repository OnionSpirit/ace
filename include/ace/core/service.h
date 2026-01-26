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
    concept is_service_templ = requires(service_t s) {
        { s.service_yank() } -> std::same_as<promise<bool>>;
    };

    template <typename derived_t>
    struct service_traits {

        service_traits() = default;

        static void crtp_asserter() {
            static_assert(is_service_templ<derived_t>,
                "Derived type doesn't have 'service_yank()' function, or it's return type is not 'ace::promise<bool>'");
            static_assert(std::derived_from<derived_t, service_traits>,
                "Derived type is not actually derived from 'service_traits<DerivedT>'");
        };

        void respawn() {
            dispatcher.spawn(service(dispatcher_sig_pipe));
            _detached = false;
        }

        void attach() {
            if (_detached) {
                _detached = false;
                respawn();
            }
        }

        bool _detached {true};

        async<> service(sig_pipe_t& sig_pipe) {
            std::unique_ptr<signal_handler> sig { nullptr };
            while (not _detached) {
                _detached = co_await static_cast<derived_t*>(this)->service_yank();
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

    template <typename derived_t>
    class global_service_traits {

        void respawn() {
            dispatcher.spawn(service(dispatcher_sig_pipe));
            _detached = false;
        }

        static void crtp_asserter() {
            static_assert(is_service_templ<derived_t>,
                "Derived type doesn't have 'service_yank()' function, or it's return type is not 'ace::promise<bool>'");
            static_assert(std::derived_from<derived_t, global_service_traits>,
                "Derived type is not actually derived");
        };

        global_service_traits() {
            crtp_asserter();
            respawn();
        };

        bool _detached {false};

    friend derived_t;
    public:

        static derived_t& get_instance() {
            static derived_t instance;
            if (instance._detached) instance.respawn();
            return instance;
        }

        // NOTE: The derived type's 'service_yank()' function shall return
        // NOTE: the result of the service's job completion. If the service still
        // NOTE: has unfinished work, the function shall return 'false'. If the service
        // NOTE: has no unfinished work, the function shall return 'true'.
        async<> service(sig_pipe_t& sig_pipe) {
            std::unique_ptr<signal_handler> sig { nullptr };
            while (not _detached) {
                _detached = co_await static_cast<derived_t*>(this)->service_yank();
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
    concept is_service = is_service_templ<service_t>
        and (
            std::derived_from<service_t, global_service_traits<service_t>>
            or std::derived_from<service_t, service_traits<service_t>>
        );
}

#endif //ACE_SERVICE_H
