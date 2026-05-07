/**
* @file dispatcher.h
 * @brief Multi-threaded task dispatcher that distributes coroutines across runners.
 *
 * @details The @c dispatcher is the multi-thread layer of the ACE runtime.  It
 * owns a vector of @c runner objects — one per OS thread — and drives them
 * through a coordinated polling loop.
 *
 * ### Thread model
 *
 * - The <b>main thread</b> runs @c runner[0] directly from @c dispatcher::run().
 * - Each <b>worker thread</b> (jthread) runs its own @c runner[i] in a tight loop.
 * - All threads call @c worker_round() which processes tasks for up to 1 ms,
 *   then sleeps for 1 ms if no tasks were processed.
 * - @c run() blocks until all runners have reported @c _pending = true
 *   simultaneously (all queues empty).
 *
 * ### Task assignment
 *
 * New tasks are assigned to runners via a round-robin atomic counter
 * (@c _runner_selector).  A specific runner can also be targeted by passing a
 * non-null @c runner* to @c schedule().
 *
 * @see ace::core::runner, ace::core::dispatcher, ace::core::s_dispatcher_config
 */

#ifndef ACE_CORE_DISPATCHER_H
#define ACE_CORE_DISPATCHER_H

#include <thread>
#include <random>
#include <functional>

#include "ace/core/tools/meta.h"
#include "ace/core/runner.h"
#include "ace/core/signal.h"

namespace ace {

    inline bool reload() noexcept;

    inline void schedule(task &&new_task, core::runner* = nullptr) noexcept;

    inline void run() noexcept;

    inline bool empty() noexcept;

    inline void reset_signal();

    inline void interrupt();

    inline void terminate();

}

namespace ace::core {

    /**
     * @brief Global configuration for the dispatcher.
     *
     * @details Modify @c s_dispatcher_config before calling @c ace::reload() to
     * change the number of runner threads.  The reload takes effect only when
     * all queues are empty.
     *
     * @par Example
     * @code{.cpp}
     * ace::core::s_dispatcher_config._runners_amount = 4;
     * ace::reload();
     * @endcode
     */
    struct dispatcher_config {
        std::size_t _runners_amount{1}; ///< Number of runner threads (including the main thread).
        bool operator==(const dispatcher_config &dispatcher_config) const = default;
    } inline s_dispatcher_config{}; ///< Global singleton configuration instance.

    /**
     * @brief Schedules and drives task execution across multiple runner threads.
     *
     * @details @c dispatcher is the core multi-thread scheduler.  It creates one
     * @c runner per configured thread, launches worker @c jthreads for runners
     * 1..N-1, and runs runner 0 on the calling thread inside @c run().
     *
     * The @c run() call blocks until all runners are idle simultaneously.
     * Tasks are distributed round-robin unless a specific runner is specified.
     */
    class dispatcher {

        dispatcher() {
            fetch_config();
            _runners.resize(_dispatcher_config._runners_amount);
            _workers_states.resize(_dispatcher_config._runners_amount);
        };

        static thread_local std::chrono::time_point<std::chrono::steady_clock> local_ts;

        static void fetch_time() { local_ts = std::chrono::steady_clock::now(); }

        /**
         * @brief Per-thread status record.  Cache-line aligned to prevent
         * false sharing between worker threads.
         */
        struct alignas(ACE_CACHE_LINE_SIZE) worker_state {
            int  _worker_id  { 0 };      ///< Zero-based index of this worker's runner.
            bool _pending    { false };  ///< @c true when the runner found no tasks in the last round.
            int  _rounds     { 0 };      ///< Number of consecutive 1 ms work rounds completed.
        };

        ACE_CACHE_LINE(0)

        std::vector<runner>        _runners             { };
        std::vector<worker_state>  _workers_states      { };
        std::atomic<double>        _aggregate_velocity  { };
        dispatcher_config          _dispatcher_config   { };

        ACE_CACHE_LINE(1)

        std::atomic<uint64_t>      _runner_selector     { };

        ACE_CACHE_LINE(2)

        sig_pipe_t _sig_pipe{};

        void worker_round(const int worker_id) {
            using namespace std::chrono_literals;

            // NOTE: Flag that indicates that runner processed some tasks in the interval
            bool active = false;

            // NOTE: Timepoint to track interval
            const auto start = get_time();
            auto now = start;

            // NOTE: Working with runner until interval ends (also updating last ts)
            const bool velocity_tracking = _dispatcher_config._runners_amount > 1;
            bool is_polling = false;
            while (now - start < 1ms) {
                active = _runners[worker_id].run() or active;
                fetch_time();
            now = get_time();
                is_polling = _runners[worker_id].is_polling();
                if (velocity_tracking and (now - start >= 1ms or is_polling)) {
                    const double old_velocity = _runners[worker_id].velocity();
                    const double new_velocity = _runners[worker_id].upgrade_velocity(now - start);
                    _aggregate_velocity.fetch_add(new_velocity,std::memory_order_acquire);
                    _aggregate_velocity.fetch_sub(old_velocity, std::memory_order_release);
                }
                // NOTE: Breaking if runner is on polling state to make sleep
                if (is_polling) break;
            }

            // NOTE: Updating runner status
            _workers_states[worker_id]._pending = not active;
            ++_workers_states[worker_id]._rounds;

            // NOTE: Making decision about sleeping
            if (is_polling)
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            else if (not active or _workers_states[worker_id]._rounds > 999) {
                _workers_states[worker_id]._rounds = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        void worker_tf(const std::stop_token &stoken, const int worker_id) {
            _workers_states[worker_id]._worker_id = worker_id;
            _workers_states[worker_id]._pending = false;
            while (not stoken.stop_requested())
                worker_round(worker_id);
        }

        void fetch_config() noexcept { _dispatcher_config = s_dispatcher_config; }

        static constexpr uint8_t _min_service_skips = 3;

        void round_robin(task &&new_task) noexcept {
            const auto runner_id = _runner_selector.fetch_add(1, std::memory_order_relaxed);
            _runners[runner_id % _dispatcher_config._runners_amount].attach(std::forward<task>(new_task));
        }

        static auto get_time()
            -> std::chrono::time_point<std::chrono::steady_clock> { return local_ts; }

        static dispatcher &get_instance() noexcept {
            static dispatcher instance;
            return instance;
        }

    public:

        static sig_pipe_t &get_sig_pipe() noexcept {
            return get_instance()._sig_pipe;
        }

        friend inline bool ace::reload() noexcept;

        friend inline void ace::schedule(task &&new_task, core::runner*) noexcept;

        friend inline void ace::run() noexcept;

        friend inline bool ace::empty() noexcept;

        friend inline void ace::reset_signal();

        friend inline void ace::interrupt();

        friend inline void ace::terminate();

        dispatcher(const dispatcher &) = delete;

        dispatcher(dispatcher &&) = delete;

        dispatcher &operator=(const dispatcher &) = delete;

        dispatcher &operator=(dispatcher &&) = delete;
    };

} // end namespace ace::core

thread_local std::chrono::time_point<std::chrono::steady_clock> ace::core::dispatcher::local_ts
        = std::chrono::steady_clock::now();

namespace ace {

    /**
     * @details Checks if any Tasks stored in any of the runners
     * @return @b true if empty, @b false otherwise
     */
    [[nodiscard]] inline bool empty() noexcept {
        const auto& self = core::dispatcher::get_instance();
        bool res{true};
        for (std::size_t runner_id = 0; runner_id < self._runners.size() and res; ++runner_id)
            res &= self._runners[runner_id].empty();
        return res;
    };

    /**
     * @brief Reloads dispatcher configuration
     */
    inline bool reload() noexcept {
        auto& self = core::dispatcher::get_instance();
        if (self._dispatcher_config == core::s_dispatcher_config) return true;
        if (not empty()) return false;
        self.fetch_config();
        self._runners.clear();
        self._runners.resize(self._dispatcher_config._runners_amount);
        self._workers_states.clear();
        self._workers_states.resize(self._dispatcher_config._runners_amount);
        return true;
    }

    /**
     * @brief Function to schedule task at the dispatcher
     * @param new_task Task to be pushed into the dispatcher
     * @param rnr Specific runner to schedule on
     * @return void
     */
    inline void schedule(task &&new_task, core::runner *rnr) noexcept {
        new_task._coroutine.promise()._roaming = true;
        auto& self = core::dispatcher::get_instance();
        if (not rnr) {
            // NOTE: No balancing for single runner
            if (self._dispatcher_config._runners_amount == 1) {
                self._runners[0].attach(std::forward<task>(new_task));
                return;
            }
            // NOTE: Round-Robin balancing on Zero score count
            if (self._aggregate_velocity.load() < 1.0) {
                self.round_robin(std::move(new_task));
                return;
            }
            // NOTE: Probability accumulation selection on charged runners
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_real_distribution<> distrib(0.0, 1.0);

            const double probability{distrib(gen)};
            double attractiveness_accumulator{};

            for (auto &runner: self._runners) {
                const double runner_attractiveness = 1.0 - (runner.velocity() / self._aggregate_velocity.load());
                attractiveness_accumulator += runner_attractiveness;
                if (probability <= attractiveness_accumulator) {
                    runner.attach(std::forward<task>(new_task));
                    return;
                }
            }
            // NOTE: Round-Robin balancing on probability accumulation miss
            self.round_robin(std::move(new_task));
        } else {
            new_task._coroutine.promise()._roaming = false;
            rnr->attach(std::forward<task>(new_task));
        }
    }

    /**
     * @details Resumes all tasks from the ready task pool until it is empty.
     */
    inline void run() noexcept {

        auto& self = core::dispatcher::get_instance();
        const int workers_amount = static_cast<int>(self._dispatcher_config._runners_amount);

        // NOTE: Clearing velocity to make it zero before run
        for (auto &runner: self._runners) runner.clear_velocity();
        self._aggregate_velocity.store(0.0, std::memory_order_release);

        do {
            // NOTE: Initiating
            std::vector<std::jthread> workers{};

            // NOTE: Launching
            workers.reserve(workers_amount - 1);
            for (int worker_id = 1; worker_id < workers_amount; ++worker_id)
                workers.emplace_back(std::bind_front(&core::dispatcher::worker_tf, &self), worker_id);

            // NOTE: Polling
            bool is_running{true};
            while (is_running) {
                // NOTE: Doing main thread job
                self.worker_round(0);
                // NOTE: Checking other threads for finish
                bool is_pending{true};
                for (int worker_id = 0; is_pending and worker_id < workers_amount; ++worker_id) {
                    is_pending = self._workers_states[worker_id]._pending;
                    is_running = not is_pending or worker_id not_eq workers_amount - 1;
                }
            }
        } while (not empty());
    }

    inline void reset_signal() {
        std::unique_ptr<core::signal_handler> sgl;
        while (not core::dispatcher::get_sig_pipe().pop(sgl) and not core::dispatcher::get_sig_pipe().empty())
            sgl.reset();
    }

    inline void interrupt() {
        core::dispatcher::get_sig_pipe().push(ace::core::make_signal(ace::core::interruption_signal{}));
    }

    inline void terminate() {
        core::dispatcher::get_sig_pipe().push(ace::core::make_signal(ace::core::termination_signal{}));
    }

} // end namespace ace

#endif // ACE_CORE_DISPATCHER_H
