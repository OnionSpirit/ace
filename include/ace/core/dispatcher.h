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
 * - Persistent <b>worker threads</b> drive runners 1..N-1 and sleep on atomic
 *   epochs while no run is active or their runner has no runnable work.
 * - @c run() drives runner 0 and blocks until every published per-runner load is zero.
 *
 * ### Task assignment
 *
 * Automatic assignment uses power-of-two choices: a rotating anchor and one
 * pseudo-random peer are sampled, and the lower published runnable load wins.
 * Selection and load updates are O(1) and contain no shared random generator.
 *
 * @see ace::core::runner, ace::core::dispatcher, ace::cfg::param
 */

#ifndef ACE_CORE_DISPATCHER_H
#define ACE_CORE_DISPATCHER_H

#include <algorithm>
#include <functional>
#include <thread>

#include "ace/core/tools/macro.h"
#include "ace/core/runner.h"
#include "ace/core/signal.h"
#include "ace/core/config.h"

namespace ace {

    /**
     * @brief Reconfigure the number of runners.
     * @return @c true on success; @c false for zero runners, active work, an
     *         active @c run(), or allocation/thread shutdown failure.
     */
    inline bool reload() noexcept;

    /**
     * @brief Schedule a task for execution.
     * @param new_task Task to schedule.
     * @param rnr      Target runner; @c nullptr for automatic selection.
     */
    inline void schedule(task &&new_task, core::runner* = nullptr);

    /// @brief Execute until the published runnable load reaches quiescence.
    /// @throws std::logic_error when another @c run() is already active.
    /// @throws std::system_error when persistent worker construction fails.
    inline void run();

    /// @brief Check whether all runners are empty (no pending tasks).
    inline bool empty();

    /// @brief Drain the signal pipe, discarding all pending signals.
    inline void reset_signal();

    /// @brief Send an interruption signal to all service routines.
    inline void interrupt();

    /// @brief Send a termination signal to all service routines.
    inline void terminate();

}

namespace ace::core {

    /**
     * @brief Schedules and drives task execution across multiple runner threads.
     *
     * @details @c dispatcher is the core multi-thread scheduler.  It creates one
     * @c runner per configured thread, launches worker @c jthreads for runners
     * 1..N-1, and runs runner 0 on the calling thread inside @c run().
     *
     * The @c run() call blocks until all runner loads are zero. Tasks are
     * distributed by bounded load-aware sampling unless explicitly targeted.
     */
    class dispatcher {

        /// @brief Creates runners and worker states from the current configuration.
        dispatcher() {
            const std::size_t initial_runners = std::max<std::size_t>(
                cfg::g_config._runners_amount, 1);
            _runners.resize(initial_runners);
            bind_runners();
        }

        ~dispatcher() { stop_workers(); }

        /**
         * @brief Per-thread status record.  Cache-line aligned to prevent
         * false sharing between worker threads.
         */
        struct selector_state {
            std::uint64_t _random { 0x9e3779b97f4a7c15ULL }; ///< Per-thread pseudo-random state.
            std::size_t   _cursor {};                            ///< Round-robin tie-breaking anchor.
            std::size_t   _runners {};                           ///< Runner count used to initialize the cursor.
        };

        static thread_local selector_state _selector_state; ///< Contention-free automatic-selection state.

        ACE_CACHE_LINE(0)

        std::vector<runner>        _runners             { };  ///< Per-thread runners (index == thread id).
        std::vector<std::jthread>  _workers             { };  ///< Persistent workers for runners 1..N-1.

        ACE_CACHE_LINE(1)

        std::atomic<std::uint64_t> _activity_epoch      { };  ///< Runner active/idle transition sequence.
        std::atomic_bool           _run_active          { false }; ///< Whether a caller currently drives @c run().

        ACE_CACHE_LINE(2)

        sig_pipe_t _sig_pipe{};  ///< Signal pipe shared with all service routines.

        /**
         * @brief Applies bounded adaptive backoff to polling services.
         * @param rounds Consecutive polling rounds, updated in place.
         */
        static void polling_backoff(std::uint32_t& rounds) noexcept;

        /**
         * @brief Worker thread entry — runs @c worker_round() until stop is requested.
         * @param stoken    Stop token for thread shutdown.
         * @param worker_id Zero-based index of the worker's runner.
         */
        void worker_tf(const std::stop_token &stoken, std::size_t worker_id) noexcept;

        /// @brief Binds every runner to this dispatcher's activity notification.
        void bind_runners() noexcept;

        /// @brief Creates persistent workers with rollback on thread-construction failure.
        void ensure_workers();

        /// @brief Requests stop, wakes and joins all persistent workers.
        void stop_workers() noexcept;

        /**
         * @brief Samples two runners and selects the lower published load.
         * @return Selected runner index.
         */
        [[nodiscard]] std::size_t select_runner() noexcept;

        /**
         * @brief Returns the dispatcher singleton.
         * @return Reference to the unique dispatcher instance.
         */
        static dispatcher &get_instance() {
            // Configure Nukes before constructing runner queues: their dummy
            // nodes are the first dynamic queue allocations in a fresh ACE process.
            (void)configure_nukes_node_allocator();
            static dispatcher instance;
            return instance;
        }

    public:

        /**
         * @brief Returns the dispatcher's signal pipe.
         * @return Reference to the shared signal pipe.
         */
        static sig_pipe_t &get_sig_pipe() {
            return get_instance()._sig_pipe;
        }

        friend inline bool ace::reload() noexcept;

        friend inline void ace::schedule(task &&new_task, core::runner*);

        friend inline void ace::run();

        friend inline bool ace::empty();

        friend inline void ace::reset_signal();

        friend inline void ace::interrupt();

        friend inline void ace::terminate();

        /// @brief Copying a dispatcher is forbidden (singleton).
        dispatcher(const dispatcher &) = delete;

        /// @brief Moving a dispatcher is forbidden (singleton).
        dispatcher(dispatcher &&) = delete;

        /// @brief Copy assignment is forbidden (singleton).
        dispatcher &operator=(const dispatcher &) = delete;

        /// @brief Move assignment is forbidden (singleton).
        dispatcher &operator=(dispatcher &&) = delete;
    };

} // end namespace ace::core

inline thread_local ace::core::dispatcher::selector_state
    ace::core::dispatcher::_selector_state {};

inline void ace::core::dispatcher::polling_backoff(std::uint32_t& rounds) noexcept {
    if (rounds < 16) {
        ++rounds;
        std::this_thread::yield();
        return;
    }
    const std::uint32_t shift = std::min<std::uint32_t>(rounds - 16, 10);
    const auto delay = std::chrono::microseconds { std::uint32_t {1} << shift };
    if (rounds < 26)
        ++rounds;
    std::this_thread::sleep_for(delay);
}

inline void ace::core::dispatcher::bind_runners() noexcept {
    for (auto& runner : _runners)
        runner.bind_activity_epoch(&_activity_epoch);
}

inline void ace::core::dispatcher::stop_workers() noexcept {
    for (auto& worker : _workers)
        worker.request_stop();
    for (std::size_t runner_id = 1; runner_id < _runners.size(); ++runner_id)
        _runners[runner_id].notify_worker();
    _workers.clear();
}

inline void ace::core::dispatcher::ensure_workers() {
    const std::size_t required = _runners.size() - 1;
    if (_workers.size() == required)
        return;

    stop_workers();
    try {
        _workers.reserve(required);
        for (std::size_t runner_id = 1; runner_id < _runners.size(); ++runner_id)
            _workers.emplace_back(
                std::bind_front(&dispatcher::worker_tf, this), runner_id);
    } catch (...) {
        stop_workers();
        throw;
    }
}

inline void ace::core::dispatcher::worker_tf(
    const std::stop_token& stoken,
    const std::size_t worker_id) noexcept
{
    runner& local_runner = _runners[worker_id];
    std::stop_callback wake_on_stop {
        stoken,
        [&local_runner] { local_runner.notify_worker(); }
    };
    std::uint32_t polling_rounds = 0;

    while (not stoken.stop_requested()) {
        const std::uint64_t observed = local_runner.wake_epoch();
        if (not _run_active.load(std::memory_order_acquire) or local_runner.load() == 0) {
            if (not stoken.stop_requested()
                and (not _run_active.load(std::memory_order_acquire) or local_runner.load() == 0))
                local_runner.wait_for_work(observed);
            continue;
        }

        const bool progressed = local_runner.run();
        if (local_runner.is_polling())
            polling_backoff(polling_rounds);
        else
            polling_rounds = 0;
        if (not progressed)
            std::this_thread::yield();
    }
}

inline std::size_t ace::core::dispatcher::select_runner() noexcept {
    const std::size_t runners = _runners.size();
    selector_state& state = _selector_state;
    if (state._runners != runners) {
        state._runners = runners;
        state._random ^= static_cast<std::uint64_t>(
            std::hash<std::thread::id> {}(std::this_thread::get_id()));
        if (state._random == 0)
            state._random = 0x9e3779b97f4a7c15ULL;
        state._cursor = static_cast<std::size_t>(state._random % runners);
    }

    const std::size_t first = state._cursor;
    if (++state._cursor == runners)
        state._cursor = 0;

    state._random ^= state._random << 13;
    state._random ^= state._random >> 7;
    state._random ^= state._random << 17;
    const std::size_t offset = 1 + static_cast<std::size_t>(state._random % (runners - 1));
    std::size_t second = first + offset;
    if (second >= runners)
        second -= runners;

    const std::size_t first_load = _runners[first].load();
    const std::size_t second_load = _runners[second].load();
    return first_load <= second_load ? first : second;
}

namespace ace {

    /**
     * @brief Check whether all runners are empty (no pending tasks).
     * @return @c true if empty, @c false otherwise.
     */
    [[nodiscard]] inline bool empty() {
        const auto& self = core::dispatcher::get_instance();
        for (const auto& runner : self._runners) {
            if (not runner.quiescent())
                return false;
        }
        return true;
    };

    /**
     * @brief Reload the balancer configuration.
     * @details Only takes effect when all queues are empty.
     * @return @c true on success, @c false if queues are not empty.
     */
    inline bool reload() noexcept {
        try {
            auto& self = core::dispatcher::get_instance();
            const std::size_t candidate = cfg::g_config._runners_amount;
            if (candidate == 0)
                return false;
            if (candidate == self._runners.size())
                return true;
            if (self._run_active.load(std::memory_order_acquire) or not empty())
                return false;

            std::vector<core::runner> new_runners(candidate);
            self.stop_workers();
            self._runners.swap(new_runners);
            self.bind_runners();
            return true;
        } catch (...) {
            return false;
        }
    }

    /**
     * @brief Schedule a task for execution.
     * @details Automatic selection samples two runners in O(1); an explicit
     * target bypasses balancing and disables roaming.
     * @throws std::bad_alloc when task-node publication cannot allocate storage.
     */
    inline void schedule(task &&new_task, core::runner *rnr) {
        // TODO: I will return it back when I will create spawn groups
        // new_task._coroutine.promise()._roaming = true;
        auto& self = core::dispatcher::get_instance();
        if (not rnr) {
            if (self._runners.size() == 1) {
                self._runners[0].attach(std::forward<task>(new_task));
                return;
            }
            self._runners[self.select_runner()].attach(std::move(new_task));
        } else {
            new_task._coroutine.promise()._roaming = false;
            rnr->attach(std::forward<task>(new_task));
        }
    }

    /**
     * @brief Execute all scheduled tasks — blocks until the queue is empty.
     * @details Launches worker threads for runners 1..N-1, runs runner 0
     * on the calling thread, and polls until all runners report no tasks in an
     * activity-epoch-stable snapshot.
     */
    inline void run() {
        auto& self = core::dispatcher::get_instance();
        bool expected = false;
        if (not self._run_active.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel))
            throw std::logic_error { "ace::run() is already active" };

        try {
            self.ensure_workers();
        } catch (...) {
            self._run_active.store(false, std::memory_order_release);
            throw;
        }

        for (std::size_t runner_id = 1; runner_id < self._runners.size(); ++runner_id)
            self._runners[runner_id].notify_worker();

        std::uint32_t polling_rounds = 0;
        while (true) {
            auto& main_runner = self._runners[0];
            if (not main_runner.quiescent()) {
                const bool progressed = main_runner.run();
                if (main_runner.is_polling())
                    core::dispatcher::polling_backoff(polling_rounds);
                else
                    polling_rounds = 0;
                if (not progressed)
                    std::this_thread::yield();
                continue;
            }

            const std::uint64_t observed = self._activity_epoch.load(std::memory_order_acquire);
            if (empty()) {
                // A runnable node may migrate from a runner not yet visited by
                // empty() to one already visited. Accept the O(N) snapshot only
                // if no 0->1 or 1->0 transition occurred during the scan.
                if (self._activity_epoch.load(std::memory_order_acquire) == observed)
                    break;
                continue;
            }
            if (main_runner.quiescent())
                self._activity_epoch.wait(observed, std::memory_order_acquire);
        }

        self._run_active.store(false, std::memory_order_release);
    }

    /**
     * @brief Drain the signal pipe, discarding all pending signals.
     */
    inline void reset_signal() {
        std::unique_ptr<core::signal_handler> sgl;
        while (core::dispatcher::get_sig_pipe().pop(sgl))
            sgl.reset();
    }

    /**
     * @brief Send an interruption signal to all service routines.
     */
    inline void interrupt() {
        core::dispatcher::get_sig_pipe().push(ace::core::make_signal(ace::core::interruption_signal{}));
    }

    /**
     * @brief Send a termination signal to all service routines.
     */
    inline void terminate() {
        core::dispatcher::get_sig_pipe().push(ace::core::make_signal(ace::core::termination_signal{}));
    }

} // end namespace ace

#endif // ACE_CORE_DISPATCHER_H
