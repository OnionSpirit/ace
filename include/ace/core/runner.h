/**
* @file
 * @details This file contains a @b runner class, that executes and controls passed tasks
 */
#ifndef ACE_RUNNER_H
#define ACE_RUNNER_H
#include <optional>
#include <queue>

#include "ace/coroutines/context.h"


namespace ace::core {

/**
 * @details coroutines execution manager.
 * @tparam InitialSize Initial size of Pools
 * @tparam AllocationPolicy Pools allocation policy
 * @tparam Policies Pools policies, each policy provides independent
 * pool for coroutines
 */
class runner // : public ace::meta::technical::scheduler_id
    {

    async<>::promise_type::runner_pool_t _pool; // Note: pool of task queue
    // Note: mpsc queue to emplace task from another runner or spawning
    // std::queue<ace::promise::async<>> _input;
    // Note: signaling queue for external control
    // pool_t<int> _signals;

public:

    runner() =default;

    ~runner() =default;

    runner(runner &&t) noexcept = default;

    runner &operator=(runner &&t) noexcept = default;

    static void schedule(async<>&& p) {
        // p._coroutine.promise()._actual_pool = p._coroutine.promise()._runner_pool;
        // reinterpret_cast<task_pool_t*>(p._coroutine.promise()._actual_pool)->push(p);
    }

    /**
     * @details Resumes only one ready task
     */
    void yank() noexcept {
        coroutines::promise_touch_result touch_result = coroutines::promise_touch_result::e_blocked;
        auto* async_n = _pool.pop_node();

        /// NOTE: Pulling new context from queue
        if (not async_n) [[unlikely]] return;

        // NOTE: Proceeding context
        async_n->_data.awake(&touch_result);

        // NOTE: Checking if task can be resumed
        const bool is_resumable {
            touch_result not_eq coroutines::promise_touch_result::e_failed
            and not async_n->_data
        };

        // NOTE: Managing nodes depending on checks
        if (not is_resumable) _pool.release_node(async_n);
        else _pool.push_node(async_n);
    }

    /**
     * @details Resumes all tasks from the ready task pool until it is empty.
     */
    void run() noexcept { while(not _pool.empty()) yank(); }

    /**
     * @details Function to spawn task at the runner
     * @param new_task Task to be pushed into the runner
     * @return void
     */
    void spawn(async<>&& new_task) noexcept {
        new_task._coroutine.promise()._runner_pool = &_pool;
        _pool.push(std::forward<async<>>(new_task));
    }

    /**
     * @details Checks if any Tasks stored in the runner
     * @return @b true if empty, @b false otherwise
     */
    [[nodiscard]] bool empty() noexcept { return _pool.empty(); };
};

} // end namespace ace::core


//==============================DEFINITIONS==================================

#undef ACE_RUNNER_META
#undef ACE_RUNNER_MEMBER
#endif // ACE_RUNNER_H
