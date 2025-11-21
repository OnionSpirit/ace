/**
* @file
 * @details This file contains a @b runner class, that executes and controls passed tasks
 */
#ifndef ACE_RUNNER_H
#define ACE_RUNNER_H
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

    runner_pool_t _pool; // Note: pool of task queue
    // Note: mpsc queue to emplace task from another runner or spawning
    // std::queue<ace::promise::async<>> _input;
    // Note: signaling queue for external control
    // pool_t<int> _signals;

public:

    runner() =default;

    ~runner() =default;

    runner(runner &&t) noexcept = delete;

    runner &operator=(runner &&t) noexcept = delete;

    static void schedule(async<>&& ctx) {
        if (ctx.is_idle() or not ctx._coroutine.promise()._runner_pool)
            return;
        ctx._coroutine.promise()._runner_pool->push(std::move(ctx));
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

        // NOTE: Checking if context can be resumed
        const bool is_resumable {
            touch_result not_eq coroutines::promise_touch_result::e_failed
            and not async_n->_data
        };

        // NOTE: Checking if the context shall be forwarded via passed conductor
        const bool is_conducted {
            is_resumable
            and async_n->_data._coroutine.promise()._conductor
        };

        // NOTE: Decision if node shall be released or pushed back
        const bool is_idle = not is_resumable or is_conducted;

        // NOTE: Forwarding via conductor if needed
        if (is_conducted)
            async_n->_data._coroutine.promise()._conductor->forward(std::forward<async<>>(async_n->_data));

        // NOTE: Managing nodes depending on checks
        if (is_idle) _pool.release_node(async_n);
        else _pool.push_node(async_n);
    }

    /**
     * @details Resumes all tasks from the ready task pool until it is empty.
     */
    void run() noexcept { while(not _pool.empty()) yank(); }

    // TODO: Make return type as 'join_handler' future type, when I will write it
    /**
     * @details Function to spawn task at the runner
     * @param new_task Task to be pushed into the runner
     * @return void
     */
    template <typename async_return_t>
    void spawn(async<async_return_t>&& new_task) noexcept {
        new_task._coroutine.promise()._runner_pool = &_pool;
        _pool.push(std::forward<async<>>(async_wrap(std::forward<async<async_return_t>>(new_task))));
    }

    /**
     * @details Checks if any Tasks stored in the runner
     * @return @b true if empty, @b false otherwise
     */
    [[nodiscard]] bool empty() noexcept { return _pool.empty(); };
};

template <>
inline void runner::spawn<void>(async<>&& new_task) noexcept {
    new_task._coroutine.promise()._runner_pool = &_pool;
    _pool.push(std::forward<async<>>(new_task));
}

} // end namespace ace::core


//==============================DEFINITIONS==================================

#undef ACE_RUNNER_META
#undef ACE_RUNNER_MEMBER
#endif // ACE_RUNNER_H
