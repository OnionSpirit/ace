/**
 * @file vortex.h
 * @brief CRTP base for background polling services ("vortices").
 *
 * @details A @b vortex is a lightweight background service that runs as a
 * coroutine inside the dispatcher.  It calls its @c ping() method on every
 * scheduler iteration and suspends when it has no work to do.
 *
 * ### Lifecycle
 *
 * 1. The first call to @c touch(runner_pool) spawns the vortex coroutine into
 *    the dispatcher on the given runner (or the default runner).
 * 2. On every dispatcher iteration the vortex coroutine resumes and calls
 *    @c ping().  If @c ping() returns @c false, the vortex suspends and marks
 *    itself as detached.
 * 3. The next call to @c touch() re-spawns the vortex if it was detached.
 *
 * ### Spawn modes (@c vortex_spawn_mode)
 *
 * | Mode | Behaviour |
 * |---|---|
 * | @c e_thread_local | Each OS thread gets its own independent vortex instance. |
 * | @c e_thread_shared | A single shared vortex is used across all threads. |
 *
 * ### Example: clock vortex
 *
 * @c ace::core::clock derives from @c vortex_traits<clock, e_thread_local> and
 * implements @c bool ping() which calls @c multi_dial::release().
 *
 * @tparam derived_t      The concrete vortex type (CRTP).
 * @tparam spawn_mode_v   Spawn mode — thread-local or thread-shared.
 *
 * @see ace::core::clock, ace::common::vortex_spawn_mode
 */
#ifndef ACE_CORE_VORTEX_H
#define ACE_CORE_VORTEX_H

#include "ace/core/dispatcher.h"
#include "ace/core/signal.h"
#include "ace/core/async.h"
#include "ace/futures/polling.h"

namespace ace::core {

    enum class vortex_spawn_mode {
        e_thread_shared, ///< Single vortex for all threads
        e_thread_local,  ///< Local vortex instance for each thread
    };

    template <typename vortex_t>
    concept is_vortex_routine = requires(vortex_t v) {
        { v.ping() } -> std::same_as<bool>;
    };

    template <typename vortex_t>
    concept is_vortex_promise = requires(vortex_t v) {
        { v.ping() } -> std::same_as<promise<bool>>;
    };

    template <typename vortex_t>
    concept is_vortex_compatible = is_vortex_promise<vortex_t> or is_vortex_routine<vortex_t>;

}

namespace ace::core::traits {

    template <typename derived_t, vortex_spawn_mode spawn_mode_v>
    class vortex_traits {

        static void crtp_asserter() {
            static_assert(is_vortex_compatible<derived_t>,
                "Derived type doesn't have 'ping()' function, "
                "or it's return type is not 'ace::promise<bool>'");
            static_assert(std::derived_from<derived_t, vortex_traits>,
                "Derived type is not actually derived from 'vortex_traits<DerivedT>'");
        };

        static thread_local bool                _unique_detached;
        alignas(ACE_BUS_SIZE) std::atomic_bool  _shared_detached { true };

        void(*detach_set)(bool) = nullptr;
        bool(*detach_get)()     = nullptr;

        friend derived_t;

        static void detach_set_shared(bool b) noexcept {
            inspect()._shared_detached.store(b, std::memory_order_relaxed);
        }

        static bool detach_get_shared() noexcept {
            return inspect()._shared_detached.load(std::memory_order_relaxed);
        }

        static void detach_set_unique(const bool b) noexcept { _unique_detached = b; }

        static bool detach_get_unique() noexcept { return _unique_detached; }

        vortex_traits() {
            crtp_asserter();
            if constexpr (spawn_mode_v == vortex_spawn_mode::e_thread_local) {
                detach_set = detach_set_unique;
                detach_get = detach_get_unique;
            } else if constexpr (spawn_mode_v == vortex_spawn_mode::e_thread_shared) {
                detach_set = detach_set_shared;
                detach_get = detach_get_shared;
            } else {
                static_assert(false, "Unknown spawn mode for vortex");
            }
            // respawn();
        };

        void respawn(runner* rnr = nullptr) noexcept {
            schedule(vortex(dispatcher::get_sig_pipe()), rnr);
            detach_set(false);
        }

        task vortex(sig_pipe_t& sig_pipe) {
            std::unique_ptr<signal_handler> sig { nullptr };
            co_await futures::polling(true);
            while (not detach_get()) {
                if constexpr (is_vortex_promise<derived_t>)
                    detach_set(not co_await static_cast<derived_t*>(this)->ping());
                else if constexpr (is_vortex_routine<derived_t>)
                    detach_set(not static_cast<derived_t*>(this)->ping());
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
                co_await suspend();
            }
        }

        static derived_t& touch_impl(runner_pool_t* rnr = nullptr) noexcept {
            if constexpr (spawn_mode_v == vortex_spawn_mode::e_thread_shared) {
                static derived_t instance;
                if (instance.detach_get()) instance.respawn(pool_to_runner(rnr));
                return instance;
            } else if constexpr (spawn_mode_v == vortex_spawn_mode::e_thread_local) {
                static thread_local derived_t instance;
                if (instance.detach_get()) instance.respawn(pool_to_runner(rnr));
                return instance;
            }
        }

    public:

        // NOTE: Gets vortex instance and respawns service if needed
        static derived_t& touch(runner_pool_t* rnr = nullptr) noexcept
        requires (spawn_mode_v == vortex_spawn_mode::e_thread_shared) { return touch_impl(rnr); }

        // NOTE: Gets vortex instance and respawns service if needed
        static derived_t& touch(runner_pool_t* rnr) noexcept
        requires (spawn_mode_v == vortex_spawn_mode::e_thread_local) { return touch_impl(rnr); }

        // NOTE: Gets vortex instance to inspect
        static derived_t& inspect() noexcept {
            static derived_t instance;
            return instance;
        }

    };

    template <typename derived_t, vortex_spawn_mode spawn_mode_v>
    thread_local bool vortex_traits<derived_t, spawn_mode_v>::_unique_detached {true};

    template <typename vortex_t>
    concept is_vortex = is_vortex_compatible<vortex_t>
        and (std::derived_from<vortex_t, vortex_traits<vortex_t, vortex_spawn_mode::e_thread_local>>
            or std::derived_from<vortex_t, vortex_traits<vortex_t, vortex_spawn_mode::e_thread_shared>>);
}

#endif // ACE_CORE_VORTEX_H
