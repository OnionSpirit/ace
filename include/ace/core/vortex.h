/**
 * @file
 * @details The file contains a @b vortex class definition.
 * @b vortex aggregates requests and helps to avoid busy-polling.
 * Structurally, @b vortex is a special object with a promise for polling in the dispatcher.
 * The @b vortex polling promise maintains a detach state to stop polling when @b vortex runs out of active requests.
 * The return value of the @b yank() promise of the @b vortex derived type defines the detach state behavior.
 */
#ifndef ACE_CORE_VORTEX_H
#define ACE_CORE_VORTEX_H

#include "dispatcher.h"
#include "ace/core/signal.h"
#include "ace/coroutines/context.h"

namespace ace::core {

    template <typename vortex_t>
    concept is_vortex_routine = requires(vortex_t v) {
        { v.yank() } -> std::same_as<bool>;
    };

    template <typename vortex_t>
    concept is_vortex_promise = requires(vortex_t v) {
        { v.yank() } -> std::same_as<promise<bool>>;
    };

    template <typename vortex_t>
    concept is_vortex_compatible = is_vortex_promise<vortex_t> or is_vortex_routine<vortex_t>;

    struct shared_spawn_mode { typedef struct{} spawn_mode_t; };

    struct unique_spawn_mode { typedef struct{} spawn_mode_t; };

    template <typename mark_t>
    concept is_spawn_mode = requires { typename mark_t::spawn_mode_t; };

    template <typename derived_t, is_spawn_mode spawn_mode_t = unique_spawn_mode>
    class vortex_traits {

        static void crtp_asserter() {
            static_assert(is_vortex_compatible<derived_t>,
                "Derived type doesn't have 'yank()' function, "
                "or it's return type is not 'ace::promise<bool>'");
            static_assert(std::derived_from<derived_t, vortex_traits>,
                "Derived type is not actually derived from 'vortex_traits<DerivedT>'");
        };

        static thread_local bool                _unique_detached;
        alignas(ACE_BUS_SIZE) std::atomic_bool  _shared_detached { true };

        void(*detach_set)(bool) = nullptr;
        bool(*detach_get)()     = nullptr;

        friend derived_t;

        static void detach_set_shared(bool b) {
            get_instance()._shared_detached.store(b, std::memory_order_relaxed);
        }

        static bool detach_get_shared() {
            return get_instance()._shared_detached.load(std::memory_order_relaxed);
        }

        static void detach_set_unique(bool b) { _unique_detached = b; }

        static bool detach_get_unique() { return _unique_detached; }

        vortex_traits() {
            crtp_asserter();
            if constexpr (std::same_as<spawn_mode_t, unique_spawn_mode>) {
                detach_set = detach_set_unique;
                detach_get = detach_get_unique;
            } else if constexpr (std::same_as<spawn_mode_t, shared_spawn_mode>) {
                detach_set = detach_set_shared;
                detach_get = detach_get_shared;
            } else {
                static_assert(false, "Unknown spawn mode for vortex");
            }
            // respawn();
        };

        void respawn(runner* rnr = nullptr) {
            dispatcher::get_instance().schedule(vortex(dispatcher::get_sig_pipe()), rnr);
            detach_set(false);
        }

        async<> vortex(sig_pipe_t& sig_pipe) {
            std::unique_ptr<signal_handler> sig { nullptr };
            while (not detach_get()) {
                if constexpr (is_vortex_promise<derived_t>)
                    detach_set(not co_await static_cast<derived_t*>(this)->yank());
                else if constexpr (is_vortex_routine<derived_t>)
                    detach_set(not static_cast<derived_t*>(this)->yank());
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
            if (instance.detach_get()) instance.respawn(reinterpret_cast<runner*>(rnr));
            return instance;
        }

        static derived_t& get_instance() {
            static derived_t instance;
            return instance;
        }

    };

    template <typename derived_t, is_spawn_mode spawn_mode_t>
    thread_local bool vortex_traits<derived_t, spawn_mode_t>::_unique_detached {true};

    template <typename vortex_t>
    concept is_vortex = is_vortex_compatible<vortex_t>
        and std::derived_from<vortex_t, vortex_traits<vortex_t>>;
}

#endif // ACE_CORE_VORTEX_H
