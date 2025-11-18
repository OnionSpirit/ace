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

// TODO: Допилить объекты типа hub для менеджмента ожидающих задач, hub-объекты придут на замену пулам
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
     * @details Resumes only one ready task
     */
    void yank() noexcept {
        promises::promise_touch_result touch_result = promises::promise_touch_result::e_blocked;
        auto* task_node = _pool.pop_node();

        /// NOTE: Pulling new context from queue
        if (not task_node) [[unlikely]] return;

        // NOTE: Proceeding context
        task_node->_data.awake(&touch_result);

        // NOTE: Checking if task can be resumed
        const bool is_resumable {
            touch_result not_eq promises::promise_touch_result::e_failed
            and not task_node->_data._coroutine.done()
        };

        // NOTE: Checking if task is rescheduled
        const bool is_rescheduled {
            is_resumable
            and task_node->_data._coroutine.promise()._actual_pool not_eq &_pool
            and static_cast<task_pool_t*>(task_node->_data._coroutine.promise()._actual_pool)
                ->push(std::move(task_node->_data)) // TODO: Скорее всего нужен pool handler
        };

        // NOTE: Managing nodes depending on checks
        if (not is_resumable or is_rescheduled) _pool.release_node(task_node);
        else _pool.push_node(task_node);
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
