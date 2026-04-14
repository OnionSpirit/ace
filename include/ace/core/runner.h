/**
 * @file
 * @details This file contains a @b runner class, that executes and controls passed tasks
 */
#ifndef ACE_RUNNER_H
#define ACE_RUNNER_H
#include <queue>
#include <chrono>

#include "ace/common/terms.h"
#include "ace/coroutines/context.h"
#include "ace/common/prefetch.h"


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

    mutable runner_pool_t                  _pool;            ///< Pool of the assigned tasks
    long int                               _total_quants {}; ///< Total amount of the time quants from the all tasks on a pool
    std::optional<runner_pool_t::node_t*>  _nextup       {}; ///< Nextup task for running
    std::atomic<long int>*                 _global_total_quants {};

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
    bool yank() noexcept {
        coroutines::promise_touch_result touch_result = coroutines::promise_touch_result::e_executed;
        runner_pool_t::node_t* task_node;

        // NOTE: Taking nextup node or pulling it from a pool
        if (_nextup) [[likely]] {
            task_node = _nextup.value();
            _nextup.reset();
        } else [[unlikely]]
            task_node = _pool.pop_node();

        // NOTE: Breaking if no more available nodes with tasks
        if (not task_node) [[unlikely]] return false;

        // NOTE: Pulling next task node and prefetching it
        if (auto next_node = _pool.pop_node()) [[likely]] {
            _nextup = next_node;
            prefetch<e_l1_cache>(_nextup.value()->_data._coroutine.address());
        }

        // NOTE: Removing old quants amount
        const auto old_total_quants = _total_quants;
        _total_quants -= task_node->_data._coroutine.promise()._quants.value();

        // NOTE: Starting counter
        const auto start_time = std::chrono::steady_clock::now();

        // NOTE: Proceeding context
        task_node->_data.awake(&touch_result);

        // NOTE: Checking if context can be resumed
        const bool is_resumable {
                task_node->_data
            and touch_result not_eq coroutines::promise_touch_result::e_failed
            and touch_result not_eq coroutines::promise_touch_result::e_finished
            and touch_result not_eq coroutines::promise_touch_result::e_detached
        };

        // TODO: Separate pool onto incoming pool (for task rescheduling) and processing pool (for task processing)

        // NOTE: Checking if the context shall be forwarded via passed conductor
        const bool is_conducted {
            is_resumable
            and task_node->_data._coroutine.promise()._runner_conductor
        };

        // NOTE: Decision if node shall be released or pushed back
        const bool is_idle = not is_resumable or is_conducted;

        // NOTE: Increasing quants because task is resumable
        if (is_resumable)
            _total_quants += task_node->_data._coroutine.promise()._quants.add(
            (std::chrono::steady_clock::now() - start_time).count());

        // NOTE: Updating global total counter
        if (_global_total_quants) [[likely]]
            _global_total_quants->store(_global_total_quants->load() + _total_quants - old_total_quants);

        // NOTE: Forwarding via conductor if needed
        if (is_conducted) [[likely]]
            task_node->_data._coroutine.promise()._runner_conductor->forward(std::forward<task>(task_node->_data));

        // NOTE: If task is idle, releasing it's node. Else returning it back to the local pool
        if (is_idle) _pool.release_node(task_node);
        else _pool.push_node(task_node);

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
    bool run() noexcept {
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
