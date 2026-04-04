/**
 * @file balancer.h
 * @brief Multi-threaded task balancer that distributes coroutines across runners.
 *
 * @details The @c balancer is the multi-thread layer of the ACE runtime.  It
 * owns a vector of @c runner objects — one per OS thread — and drives them
 * through a coordinated polling loop.
 *
 * ### Thread model
 *
 * - The <b>main thread</b> runs @c runner[0] directly from @c balancer::run().
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
 * @see ace::core::runner, ace::core::dispatcher, ace::core::s_balancer_config
 */
#ifndef ACE_CORE_BALANCER_H
#define ACE_CORE_BALANCER_H

#include <cstddef>
#include <functional>
#include <thread>

#include "runner.h"
#include "ace/common/terms.h"

namespace ace::core {

    /**
     * @brief Global configuration for the balancer.
     *
     * @details Modify @c s_balancer_config before calling @c ace::reload() to
     * change the number of runner threads.  The reload takes effect only when
     * all queues are empty.
     *
     * @par Example
     * @code{.cpp}
     * ace::core::s_balancer_config._runners_amount = 4;
     * ace::reload();
     * @endcode
     */
    struct balancer_config {
        std::size_t _runners_amount { 1 }; ///< Number of runner threads (including the main thread).
        bool operator==(const balancer_config & balancer_config) const = default;
    } inline s_balancer_config {}; ///< Global singleton configuration instance.

    /**
     * @brief Schedules and drives task execution across multiple runner threads.
     *
     * @details @c balancer is the core multi-thread scheduler.  It creates one
     * @c runner per configured thread, launches worker @c jthreads for runners
     * 1..N-1, and runs runner 0 on the calling thread inside @c run().
     *
     * The @c run() call blocks until all runners are idle simultaneously.
     * Tasks are distributed round-robin unless a specific runner is specified.
     */
    class balancer {

        static thread_local std::chrono::time_point<std::chrono::steady_clock> local_ts;

        static void fetch_time() { local_ts = std::chrono::steady_clock::now(); }

        /**
         * @brief Per-thread status record.  Cache-line aligned to prevent
         * false sharing between worker threads.
         */
        struct alignas(ACE_CACHE_LINE_SIZE) worker_state {
            int _worker_id { 0 };  ///< Zero-based index of this worker's runner.
            bool _pending { false };///< @c true when the runner found no tasks in the last round.
            int _rounds {0};        ///< Number of consecutive 1 ms work rounds completed.
        };

        std::vector<runner> _runners {};
        balancer_config _balancer_config {};
        std::vector<worker_state> _workers_states {};
        std::atomic<std::size_t> _runner_selector {};

        void worker_round(const int worker_id) {
            using namespace std::chrono_literals;

            // NOTE: Flag that indicates that runner processed some tasks in the interval
            bool active = false;

            // NOTE: Timepoint to track interval
            const auto start = get_time();
            auto now = start;

            // NOTE: Working with runner until interval ends (also updating last ts)
            while (now - start < 1ms) {
                active = _runners[worker_id].run() or active;
                fetch_time();
                now = get_time();
            }

            // NOTE: Updating runner status
            _workers_states[worker_id]._pending = not active;
            ++_workers_states[worker_id]._rounds;

            // NOTE: Making decision about sleeping
            if (not active or _workers_states[worker_id]._rounds > 999) {
                _workers_states[worker_id]._rounds = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        void worker_tf(const std::stop_token& stoken, const int worker_id) {
            _workers_states[worker_id]._worker_id = worker_id;
            _workers_states[worker_id]._pending = false;
            while (not stoken.stop_requested())
                worker_round(worker_id);
        }

        void fetch_config() noexcept { _balancer_config = s_balancer_config; }

        static constexpr uint8_t _min_service_skips = 3;

    public:

        balancer() {
            fetch_config();
            _runners.resize(_balancer_config._runners_amount);
            _workers_states.resize(_balancer_config._runners_amount);
        };

        bool reload() noexcept {
            if (_balancer_config == s_balancer_config) return true;
            if (not empty()) return false;
            fetch_config();
            _runners.clear();
            _runners.resize(_balancer_config._runners_amount);
            _workers_states.clear();
            _workers_states.resize(_balancer_config._runners_amount);
            return true;
        }

        /**
         * @details Function to schedule task at the dispatcher
         * @param new_task Task to be pushed into the dispatcher
         * @param rnr Specific runner to schedule on
         * @return void
         */
        void schedule(async<>&& new_task, const runner* rnr = nullptr) noexcept {
            if (not rnr) {
                const auto runner_id = _runner_selector.fetch_add(1, std::memory_order_relaxed);
                _runners[runner_id % _balancer_config._runners_amount].attach(std::forward<async<>>(new_task));
            } else {
                rnr->attach(std::forward<async<>>(new_task));
            }
        }

        /**
         * @details Resumes all tasks from the ready task pool until it is empty.
         */
        void run() noexcept {
            // NOTE: Initiating
            std::vector<std::jthread> workers {};

            // NOTE: Launching
            const int workers_amount = static_cast<int>(_balancer_config._runners_amount);
            workers.reserve(workers_amount - 1);
            for (int worker_id = 1; worker_id < workers_amount; ++worker_id)
                workers.emplace_back(std::bind_front(&balancer::worker_tf, this), worker_id);

            // NOTE: Polling
            bool is_running { true };
            while (is_running) {
                // NOTE: Doing main thread job
                worker_round(0);
                // NOTE: Checking other threads for finish
                bool is_pending { true };
                for (int worker_id = 0; is_pending and worker_id < workers_amount; ++worker_id) {
                    is_pending = _workers_states[worker_id]._pending;
                    is_running = not is_pending or worker_id not_eq workers_amount - 1;
                }
            }
        }

        /**
         * @details Checks if any Tasks stored in any of the runners
         * @return @b true if empty, @b false otherwise
         */
        [[nodiscard]] bool empty() const noexcept {
            bool res { true };
            for (std::size_t runner_id = 0; runner_id < _runners.size() and res; ++runner_id)
                res &= _runners[runner_id].empty();
            return res;
        };

        static auto get_time()
            -> std::chrono::time_point<std::chrono::steady_clock> { return local_ts; }

        balancer(const balancer&) = delete;
        balancer(balancer&&) = delete;
        balancer& operator=(const balancer&) = delete;
        balancer& operator=(balancer&&) = delete;

    };

} // end namespace ace::core

thread_local std::chrono::time_point<std::chrono::steady_clock> ace::core::balancer::local_ts
    = std::chrono::steady_clock::now();

#endif // ACE_CORE_BALANCER_H
