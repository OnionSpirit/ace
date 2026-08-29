/**
 * @file service.h
 * @brief CRTP base for background polling services.
 *
 * @details A @b service is a lightweight background polling routine that runs as
 * a coroutine inside the dispatcher.  It calls its @c ping() method on every
 * iteration and suspends when it has no work to do.
 *
 * ### Lifecycle
 *
 * 1. The first call to @c touch(runner_pool) spawns the service coroutine into
 *    the dispatcher on the given runner (or dispatcher will select runner).
 * 2. On 16th dispatcher iteration the service coroutine resumes and calls
 *    @c ping(). If @c ping() returns @c false, the service suspends and marks
 *    itself as detached.
 * 3. The next call to @c touch() re-spawns the service if it was detached.
 *
 * ### Spawn modes (@c service_spawn_mode)
 *
 * | Mode | Behaviour |
 * |---|---|
 * | @c e_thread_local | Each OS thread gets its own independent service instance. |
 * | @c e_thread_shared | A single shared service is used across all threads. |
 *
 * ### Example: clock service
 *
 * @c ace::core::clock derives from @c service_traits<clock, e_thread_local> and
 * implements @c bool ping() which calls @c hierarchical_time_wheel::advance().
 *
 * @tparam derived_t      The concrete service type (CRTP).
 * @tparam spawn_mode_v   Spawn mode — thread-local or thread-shared.
 *
 * @see ace::core::clock, ace::core::service_spawn_mode
 */
#ifndef ACE_CORE_SERVICE_H
#define ACE_CORE_SERVICE_H

#include "ace/core/dispatcher.h"
#include "ace/core/signal.h"
#include "ace/core/async.h"
#include "ace/futures/polling.h"

namespace ace::core {

    /**
     * @brief Determines how many service instances are created.
     */
    enum class service_spawn_mode {
        e_thread_shared, ///< Single service for all threads
        e_thread_local,  ///< Local service instance for each thread
    };

    /**
     * @brief Concept: type has a synchronous @c ping() -> @c bool.
     * @tparam service_t Type to check.
     */
    template <typename service_t>
    concept is_service_routine = requires(service_t v) {
        { v.ping() } -> std::same_as<bool>;
    };

    /**
     * @brief Concept: type has an asynchronous @c ping() -> @c promise<bool>.
     * @tparam service_t Type to check.
     */
    template <typename service_t>
    concept is_service_promise = requires(service_t v) {
        { v.ping() } -> std::same_as<promise<bool>>;
    };

    /**
     * @brief Concept: type satisfies either the routine or the promise ping contract.
     * @tparam service_t Type to check.
     */
    template <typename service_t>
    concept is_service_compatible = is_service_promise<service_t> or is_service_routine<service_t>;

}

namespace ace::core::traits {

    /**
     * @brief CRTP base class for background polling services.
     *
     * @details Manages the service lifecycle: construction of the derived
     * instance, respawning when the service marked itself detached, and the
     * eternal coroutine loop (@c service()) that calls @c ping() and reacts to
     * dispatcher signals.
     *
     * @tparam derived_t      Concrete service type (CRTP).
     * @tparam spawn_mode_v   Spawn mode — thread-local or thread-shared.
     */
    template <typename derived_t, service_spawn_mode spawn_mode_v>
    class service_traits {

        /**
         * @brief Compile-time check of the derived type contract.
         */
        static void crtp_asserter() {
            static_assert(is_service_compatible<derived_t>,
                "Derived type doesn't have 'ping()' function, "
                "or it's return type is not 'ace::promise<bool>'");
            static_assert(std::derived_from<derived_t, service_traits>,
                "Derived type is not actually derived from 'service_traits<DerivedT>'");
        };

        static thread_local bool                _unique_detached; ///< Detached flag for thread-local mode
        alignas(ACE_BUS_SIZE) std::atomic_bool  _shared_detached { true }; ///< Detached flag for thread-shared mode

        void(*detach_set)(bool) = nullptr; ///< Mode-dependent detached flag setter
        bool(*detach_get)()     = nullptr; ///< Mode-dependent detached flag getter

        friend derived_t;

        /**
         * @brief Sets the shared detached flag (thread-shared mode).
         * @param b New detached value.
         */
        static void detach_set_shared(bool b) noexcept {
            inspect()._shared_detached.store(b, std::memory_order_relaxed);
        }

        /**
         * @brief Reads the shared detached flag (thread-shared mode).
         * @return Current detached value.
         */
        static bool detach_get_shared() noexcept {
            return inspect()._shared_detached.load(std::memory_order_relaxed);
        }

        /**
         * @brief Sets the unique detached flag (thread-local mode).
         * @param b New detached value.
         */
        static void detach_set_unique(const bool b) noexcept { _unique_detached = b; }

        /**
         * @brief Reads the unique detached flag (thread-local mode).
         * @return Current detached value.
         */
        static bool detach_get_unique() noexcept { return _unique_detached; }

        /**
         * @brief Constructs the base, binding the detached flag accessors
         *        according to the spawn mode.
         */
        service_traits() {
            crtp_asserter();
            if constexpr (spawn_mode_v == service_spawn_mode::e_thread_local) {
                detach_set = detach_set_unique;
                detach_get = detach_get_unique;
            } else if constexpr (spawn_mode_v == service_spawn_mode::e_thread_shared) {
                detach_set = detach_set_shared;
                detach_get = detach_get_shared;
            } else {
                static_assert(false, "Unknown spawn mode for service");
            }
            // respawn();
        };

        /**
         * @brief Schedules the service coroutine and clears the detached flag.
         * @param rnr Runner to spawn the service on; @c nullptr lets the dispatcher choose.
         */
        void respawn(runner* rnr = nullptr) {
            schedule(service(dispatcher::get_sig_pipe()), rnr);
            detach_set(false);
        }

        /**
         * @brief The eternal service coroutine loop.
         * @details Polls @c ping() (synchronously or asynchronously depending on
         * the derived type), handles dispatcher signals from the signal pipe, and
         * suspends after each iteration until the next resume.
         * @param sig_pipe Dispatcher signal pipe to react to.
         */
        task service(sig_pipe_t& sig_pipe) {
            std::unique_ptr<signal_handler> sig { nullptr };
            co_await futures::polling(true);
            while (not detach_get()) {
                if constexpr (is_service_promise<derived_t>)
                    detach_set(not co_await static_cast<derived_t*>(this)->ping());
                else if constexpr (is_service_routine<derived_t>)
                    detach_set(not static_cast<derived_t*>(this)->ping());
                if (sig_pipe.pop(sig)) [[unlikely]] {
                    const auto action_result = co_await sig->action();
                    sig_pipe.push(std::move(sig));
                    switch (action_result) {
                        case e_break:
                            co_await std::suspend_always{};
                            break;
                        case e_shutdown: co_return;
                        case e_idle: break;
                        default:;
                    }
                    sig.reset();
                }
                co_await suspend();
            }
        }

        /**
         * @brief Returns the single service instance selected by the spawn mode.
         * @return Process-wide instance for shared services or the calling
         * thread's instance for thread-local services.
         */
        static derived_t& inspect_impl() {
            if constexpr (spawn_mode_v == service_spawn_mode::e_thread_shared) {
                static derived_t instance {};
                return instance;
            } else if constexpr (spawn_mode_v == service_spawn_mode::e_thread_local) {
                thread_local derived_t instance {};
                return instance;
            }
        }

        /**
         * @brief Returns the service instance, respawning it if it was detached.
         * @param rnr Runner to spawn the service on.
         * @return Reference to the (possibly respawned) service instance.
         */
        static derived_t& touch_impl(omni_runner rnr = nullptr) {
            auto& instance = inspect_impl();
            if (instance.detach_get()) instance.respawn(rnr.as<runner>());
            return instance;
        }

    public:

        // NOTE: Gets service instance and respawns it if needed (thread-shared mode)
        static derived_t& touch(const omni_runner rnr = nullptr)
        requires (spawn_mode_v == service_spawn_mode::e_thread_shared) { return touch_impl(rnr); }

        // NOTE: Gets service instance and respawns it if needed (thread-local mode)
        static derived_t& touch(const omni_runner rnr)
        requires (spawn_mode_v == service_spawn_mode::e_thread_local) { return touch_impl(rnr); }

        /**
         * @brief Gets the same service instance as @c touch() without respawning it.
         * @return Process-wide instance for shared mode or the calling thread's
         * thread-local instance for unique mode.
         */
        static derived_t& inspect() {
            return inspect_impl();
        }

    };

    template <typename derived_t, service_spawn_mode spawn_mode_v>
    inline thread_local bool service_traits<derived_t, spawn_mode_v>::_unique_detached {true};

    /**
     * @brief Concept: type is a service — compatible and derived from @c service_traits.
     * @tparam service_t Type to check.
     */
    template <typename service_t>
    concept is_service = is_service_compatible<service_t>
        and (std::derived_from<service_t, service_traits<service_t, service_spawn_mode::e_thread_local>>
            or std::derived_from<service_t, service_traits<service_t, service_spawn_mode::e_thread_shared>>);
}

#endif // ACE_CORE_SERVICE_H
