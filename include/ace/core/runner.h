/**
* @file
 * @details This file contains a @b runner class, that executes and controls passed tasks
 */
#ifndef ACE_RUNNER_H
#define ACE_RUNNER_H
#include <optional>
#include <queue>

#include "ace/promises/async.h"
#include <nukes/dynamic/mpsc_queue.h>
#include <nukes/dynamic/mpmc_queue.h>


namespace ace::core {

template <typename T>
using pool_t = nukes::dynamic::mpsc_queue<T>;

/**
 * @details coroutines execution manager.
 * @tparam InitialSize Initial size of Pools
 * @tparam AllocationPolicy Pools allocation policy
 * @tparam Policies Pools policies, each policy provides independent
 * pool for coroutines
 */
class runner // : public ace::meta::technical::scheduler_id
    {

    typedef pool_t<promises::task> task_pool_t;
    task_pool_t _pool; // Note: pool of task queue
    // Note: mpsc queue to emplace task from another runner or spawning
    // std::queue<ace::promise::async<>> _input;
    // Note: signaling queue for external control
    pool_t<int> _signals;

public:

    runner() =default;

    ~runner() =default;

    runner(runner &&t) noexcept = default;

    runner &operator=(runner &&t) noexcept = default;

    static void schedule(promises::task&& p) {
        // p._coroutine.promise()._actual_pool = p._coroutine.promise()._runner_pool;
        // reinterpret_cast<task_pool_t*>(p._coroutine.promise()._actual_pool)->push(p);
    }

    /**
     * @details Makes one round of execution through defined pools
     * for custom ret code processing
     */
    void proceed() noexcept {
        promises::promise_touch_result touch_result = promises::promise_touch_result::e_blocked;
        task_pool_t::node_t* captured_node = _pool.pop_node();
        promises::task temp {};

        /// NOTE: Pulling new context from queue
        if (not captured_node) [[unlikely]]
            return;

        temp = std::move(captured_node->_data);

        // NOTE: Proceeding context
        temp.awake(&touch_result);

        // NOTE: Making scheduling decision
        if (touch_result not_eq promises::promise_touch_result::e_failed
            and not temp._coroutine.done()) [[likely]] {
            if (temp._coroutine.promise()._actual_pool not_eq &_pool) {
                if (static_cast<task_pool_t*>(temp._coroutine.promise()._actual_pool)->push(std::move(temp)))
                    _pool.release_node(captured_node);
            } else {
                captured_node->_data = std::move(temp);
                _pool.push_node(captured_node);
            }
        } else [[unlikely]] {
            _pool.release_node(captured_node);
        }
    }

    /**
     * @details Resumes all tasks from the ready task pool until it is empty.
     */
    void run() noexcept { while(not _pool.empty()) proceed(); }

    /**
     * @details Function to spawn task at the runner
     * @param new_task Task to be pushed into the runner
     * @return void
     */
    void spawn(promises::task&& new_task) noexcept {
        new_task._coroutine.promise()._actual_pool = reinterpret_cast<void*>(&_pool);
        _pool.push(std::forward<promises::task>(new_task));
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
