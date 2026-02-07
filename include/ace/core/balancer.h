#ifndef ACE_CORE_BALANCER_H
#define ACE_CORE_BALANCER_H

#include <cstddef>
#include <thread>

#include "runner.h"

namespace ace::core {

    struct balancer_config {
        std::size_t _runners_amount { 1 };
    };

    inline balancer_config s_balancer_config {};

    class balancer {

        const balancer_config _balancer_config;
        std::vector<runner> _runners;
        std::atomic<std::size_t> _spawn_selection_counter {};

        static void thread_function(const runner& runner) { runner.run(); }

    public:

        balancer() : _balancer_config(s_balancer_config) {
            _runners.resize(_balancer_config._runners_amount);
        };

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
         * @details Resumes all tasks from the ready task pool until it is empty.
         */
        void run() noexcept {
            std::vector<std::thread> threads;
            for (std::size_t runner_id = 1; runner_id < _runners.size() - 1; ++runner_id)
                threads.emplace_back(thread_function, std::forward<runner>(_runners[runner_id]));

            thread_function(std::forward<runner>(_runners[0]));

            for (auto& thread : threads)
                thread.join();
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
