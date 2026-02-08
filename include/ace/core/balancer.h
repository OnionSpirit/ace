#ifndef ACE_CORE_BALANCER_H
#define ACE_CORE_BALANCER_H

#include <cstddef>
#include <thread>
#include <unordered_set>

#include "runner.h"
#include "ace/common/terms.h"

namespace ace::core {

    struct balancer_config {
        std::size_t _runners_amount { 1 };
    } inline s_balancer_config {};

    class balancer {

        struct alignas(ACE_CACHE_LINE_SIZE) worker_state {
            std::size_t _worker_id { };
            bool _pending { false };
        };

        balancer_config _balancer_config {};
        std::atomic<std::size_t> _runner_selector {};

        std::vector<runner> _runners {};
        std::vector<worker_state> _workers_states {};

        runner _service_runner {};
        uint8_t _service_skips {};

        // std::stop_token stoken

        void yank_worker(std::size_t worker_id) {
            _workers_states[worker_id]._worker_id = worker_id;
            if (not _runners[worker_id].empty()) {
                _runners[worker_id].run();
                _workers_states[worker_id]._pending = false;
            }
            _workers_states[worker_id]._pending = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }

        void worker_tf(std::stop_token stoken, std::size_t worker_id) {
            while (not stoken.stop_requested())
                yank_worker(worker_id);
        }

        void fetch_config() noexcept { _balancer_config = s_balancer_config; }

        static constexpr uint8_t _min_service_skips = 3;

    public:

        balancer() {
            fetch_config();
            _runners.resize(_balancer_config._runners_amount);
            _workers_states.resize(_balancer_config._runners_amount);
        };

        void reload() noexcept {
            fetch_config();
            const auto old_runners = std::move(_runners);
            std::vector<runner> new_runners;
            new_runners.resize(_balancer_config._runners_amount);
            _runners = std::move(new_runners);
            if (new_runners.size() not_eq old_runners.size()) {
                for (auto& runner : old_runners)
                    while (auto ejected = runner.eject())
                        spawn(std::forward<async<>>(ejected.value()));
            }
        }

        // TODO: Make return type as 'join_handler' future type, when I will write it
        /**
         * @details Function to spawn task at the dispatcher
         * @param new_task Task to be pushed into the dispatcher
         * @return void
         */
        void spawn(async<>&& new_task) noexcept {
            const auto runner_id = _runner_selector.fetch_add(1, std::memory_order_relaxed);
            _runners[runner_id % _balancer_config._runners_amount].spawn(std::forward<async<>>(new_task));
        }

        /**
         * @details Function to spawn task at the dispatcher
         * @param new_service Task to be pushed into the dispatcher
         * @return void
         */
        void spawn_service(async<>&& new_service) const noexcept {
            _service_runner.spawn(std::forward<async<>>(new_service));
        }

        /**
         * @details Resumes all tasks from the ready task pool until it is empty.
         */
        void run() noexcept {

            // std::cout << "Starting run\n";

            // NOTE: Launching
            std::vector<std::jthread> workers {};
            const int runners_amount = static_cast<int>(_balancer_config._runners_amount);
            const size_t workers_amount = runners_amount - 1;
            workers.reserve(_balancer_config._runners_amount);
            for (std::size_t worker_id = 1; worker_id < workers_amount; ++worker_id) {
                workers.emplace_back(std::bind_front(&balancer::worker_tf, this), worker_id);
                // std::cout << "Launching worker " << worker_id << "\n";
            }

            bool finished { false };
            // NOTE: Polling
            while (not _service_runner.empty() or not _runners[0].empty() or not finished) {
                // NOTE: Doing main thread job
                {
                    // std::cout << "Doing main thread job\n";
                    if (++_service_skips < _min_service_skips) {
                        _service_runner.run();
                        _service_skips = 0;
                        finished = true;
                    }
                    yank_worker(0);
                }
                // NOTE: Checking other threads for finish
                {
                    // std::cout << "Checking other workers for finish\n";
                    for (int runner_id = 0; finished and runner_id < runners_amount; ++runner_id)
                        finished = (_workers_states.cbegin() + runner_id)->_pending;
                }
            }
        }

        /**
         * @details Checks if any Tasks stored in any of the runners
         * @return @b true if empty, @b false otherwise
         */
        [[nodiscard]] bool empty() const noexcept {
            bool res { _service_runner.empty() };
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
