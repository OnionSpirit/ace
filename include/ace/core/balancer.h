#ifndef ACE_CORE_BALANCER_H
#define ACE_CORE_BALANCER_H

#include <cstddef>
#include <thread>

#include "runner.h"
#include "ace/common/terms.h"

namespace ace::core {

    struct balancer_config {
        std::size_t _runners_amount { 1 };
        bool operator==(const balancer_config & balancer_config) const = default;
    } inline s_balancer_config {};

    class balancer {

        struct alignas(ACE_CACHE_LINE_SIZE) worker_state {
            int _worker_id { 0 };
            bool _pending { false };
        };

        std::vector<runner> _runners {};
        balancer_config _balancer_config {};
        std::vector<worker_state> _workers_states {};
        std::atomic<std::size_t> _runner_selector {};

        void worker_round(const int worker_id) {
            _workers_states[worker_id]._pending = not _runners[worker_id].run();
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
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

        // TODO: Make return type as 'join_handler' future type, when I will write it
        /**
         * @details Function to spawn task at the dispatcher
         * @param new_task Task to be pushed into the dispatcher
         * @param rnr Specific runner to spawn on
         * @return void
         */
        void spawn(async<>&& new_task, runner* rnr = nullptr) noexcept {
            if (not rnr) {
                const auto runner_id = _runner_selector.fetch_add(1, std::memory_order_relaxed);
                _runners[runner_id % _balancer_config._runners_amount].spawn(std::forward<async<>>(new_task));
            } else {
                rnr->spawn(std::forward<async<>>(new_task));
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

        // balancer(const balancer&) = delete;
        // balancer(balancer&&) = delete;
        // balancer& operator=(const balancer&) = delete;
        // balancer& operator=(balancer&&) = delete;

    };

} // end namespace ace::core

#endif // ACE_CORE_BALANCER_H
