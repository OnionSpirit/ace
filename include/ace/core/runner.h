/**
 * @file
 * @details This file contains a @b runner class, that executes and controls passed tasks
 */
#ifndef ACE_RUNNER_H
#define ACE_RUNNER_H
#include <queue>

#include "ace/common/terms.h"
#include "ace/coroutines/context.h"


namespace ace::core {

/**
 * @details coroutines execution manager.
 * @tparam InitialSize Initial size of Pools
 * @tparam AllocationPolicy Pools allocation policy
 * @tparam Policies Pools policies, each policy provides independent
 * pool for coroutines
 */
struct alignas(ACE_CACHE_LINE_SIZE) runner {

    ACE_CACHE_LINE(0)

    mutable runner_pool_t _pool; // Note: pool of task queue

    runner() =default;
    // TODO: Need to figure out how to validate this wo warn cuz its important
    // {
    //     static_assert(offsetof(runner, _pool) == 0,
    //         "'_pool' must be the first member of runner. Stop touching not your code idiot");
    // };

    ~runner() =default;

    runner(runner &&t) noexcept {
        this->_pool = std::move(t._pool);
    };

    runner &operator=(runner &&t) noexcept {
        this->_pool = std::move(t._pool);
        return *this;
    };

    /**
     * @brief Returns task into source @b runner
     * @param ctx Task to be reattached into @b runner
     */
    static void reattach(task&& ctx) {
        if (not ctx.is_resumable() or not ctx._coroutine.promise()._runner_pool)
            return;
        ctx._coroutine.promise()._runner_pool->push(std::move(ctx));
    }

    /**
     * @details Resumes only one ready task
     * @return @b true if task was processed, @b false otherwise
     */
    bool yank() const noexcept {
        coroutines::promise_touch_result touch_result = coroutines::promise_touch_result::e_executed;
        auto* async_n = _pool.pop_node();

        /// NOTE: Pulling new context from queue
        if (not async_n) [[unlikely]] return false;

        // NOTE: Proceeding context
        async_n->_data.awake(&touch_result);

        // NOTE: Checking if context can be resumed
        const bool is_resumable {
                async_n->_data
            and touch_result not_eq coroutines::promise_touch_result::e_failed
            and touch_result not_eq coroutines::promise_touch_result::e_finished
            and touch_result not_eq coroutines::promise_touch_result::e_detached
        };

        // TODO: Separate pool onto incoming pool (for task rescheduling) and processing pool (for task processing)

        // NOTE: Checking if the context shall be forwarded via passed conductor
        const bool is_conducted {
            is_resumable
            and async_n->_data._coroutine.promise()._runner_conductor
        };

        // NOTE: Decision if node shall be released or pushed back
        const bool is_idle = not is_resumable or is_conducted;

        // NOTE: Forwarding via conductor if needed
        if (is_conducted) [[likely]]
            async_n->_data._coroutine.promise()._runner_conductor->forward(std::forward<task>(async_n->_data));

        // NOTE: If async is idle, releasing it's node. Else returning it back to the local pool
        if (is_idle) _pool.release_node(async_n);
        else _pool.push_node(async_n);

        return true;
    }

    /**
     * @brief Ejects task from runner
     * @return Optional of ejected task
     */
    std::optional<task> eject() const noexcept {
        if (task ejective; _pool.pop(ejective)) [[likely]]
            return ejective;
        return std::nullopt;
    }

    /**
     * @details Resumes tasks from the ready task pool until it is empty, or limit (1024) reached.
     * @return @b true if runner made some tasks, @b false otherwise
     */
    bool run() const noexcept {
        int i = 0;
        constexpr int yank_limit = 128;
        while (i < yank_limit and yank()) ++i;
        return i not_eq 0;
        // NOTE: Old return
        // return i == yank_limit;
    }

    // TODO: Make return type as 'join_handler' future type, when I will write it
    /**
     * @details Function to attach task to the runner
     * @param new_task Task to be pushed into the runner
     * @return void
     */
    template <typename async_return_t>
    void attach(async<async_return_t>&& new_task) const noexcept {
        new_task._coroutine.promise()._runner_pool = &_pool;
        _pool.push(std::forward<task>(async_wrap(std::forward<async<async_return_t>>(new_task))));
    }

    /**
     * @details Checks if any Tasks stored in the runner
     * @return @b true if empty, @b false otherwise
     */
    [[nodiscard]] bool empty() const noexcept { return _pool.empty(); };
};

template <>
inline void runner::attach<void>(task&& new_task) const noexcept {
    new_task._coroutine.promise()._runner_pool = &_pool;
    _pool.push(std::forward<task>(new_task));
}

inline auto pool_to_runner(runner_pool_t* pool) noexcept {
    return reinterpret_cast<runner*>(pool);
}

} // end namespace ace::core


//==============================DEFINITIONS==================================

#undef ACE_RUNNER_META
#undef ACE_RUNNER_MEMBER
#endif // ACE_RUNNER_H
