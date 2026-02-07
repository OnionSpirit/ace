#ifndef ACE_CORE_BALANCER_H
#define ACE_CORE_BALANCER_H

#include <cstddef>
#include <thread>

#include "runner.h"

namespace ace::core {

    struct balancer_config {
        std::size_t _runners_amount { 1 };
    } inline s_balancer_config {};

    class balancer {

        balancer_config _balancer_config {};
        std::vector<runner> _runners {};
        runner _service_runner {};
        uint8_t _service_skips {};
        std::atomic<std::size_t> _spawn_selection_counter {};

        static void thread_function(const runner& runner) { runner.run(); }

        void fetch_config() noexcept { _balancer_config = s_balancer_config; }

        static constexpr uint8_t _min_service_skips = 3;

    public:

        balancer() {
            fetch_config();
            _runners.resize(_balancer_config._runners_amount);
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
            const auto runner_id = _spawn_selection_counter.fetch_add(1, std::memory_order_relaxed);
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
            std::vector<std::thread> threads;
            threads.reserve(_balancer_config._runners_amount);
            for (std::size_t runner_id = 1; runner_id < (_runners.size() - 1); ++runner_id)
                threads.emplace_back(thread_function, std::ref(_runners[runner_id]));

            thread_function(std::ref(_runners[0]));
            if (++_service_skips < _min_service_skips) {
                thread_function(std::ref(_service_runner));
                _service_skips = 0;
            }

            for (auto& thread : threads)
                thread.join();
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
