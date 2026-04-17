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
#include "nukes/dynamic/mpsc_queue.h"


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

    typedef nukes::dynamic::mpsc_queue<task> insert_pool_t;

    mutable runner_pool_t                    _pool            {}; ///< Pool of the assigned tasks
    std::optional<task>                      _nextup          {}; ///< Nextup task for running
    std::atomic<int>*                        _common_quants   {}; ///< Pointer to common quant counter
    int                                      _total_quants    {}; ///< Total amount of the time quants from the all tasks on a pool
    mutable nukes::dynamic::mpsc_queue<task> _insert_pool     {}; ///< Pool for the interthread insertion

    runner() =default;
    // TODO: Need to figure out how to validate this wo warn cuz its important
    // {
    //     static_assert(offsetof(runner, _pool) == 0,
    //         "'_pool' must be the first member of runner. Stop touching not your code idiot");
    // };

    ~runner() =default;

    runner(runner &&t) noexcept {
        this->_pool = std::move(t._pool);
        this->_nextup = std::move(t._nextup);
        t._nextup = std::nullopt;
        this->_common_quants = t._common_quants;
        t._common_quants = nullptr;
        this->_total_quants = t._total_quants;
        t._total_quants = 0;
        this->_insert_pool = std::move(t._insert_pool);
    };

    runner &operator=(runner &&t) noexcept {
        this->_pool = std::move(t._pool);
        this->_nextup = std::move(t._nextup);
        t._nextup = std::nullopt;
        this->_common_quants = t._common_quants;
        t._common_quants = nullptr;
        this->_total_quants = t._total_quants;
        t._total_quants = 0;
        this->_insert_pool = std::move(t._insert_pool);
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
     * @brief Returns task into source @b runner
     * @param ctx Task to be reattached into @b runner
     */
    static void interthread_reattach(task&& ctx) {
        if (not ctx.is_resumable() or not ctx._coroutine.promise()._runner_pool)
            return;
        (reinterpret_cast<runner*>(ctx._coroutine.promise()._runner_pool)->*(&runner::_insert_pool)).push(std::move(ctx));
    }

    /**
     * @details Resumes only one ready task
     * @return @b true if task was processed, @b false otherwise
     */
    bool yank() noexcept {

        insert_pool_t::node_t* interthread_task;
        while ((interthread_task = _insert_pool.pop_node())) {
            _pool.push(std::move(interthread_task->_data));
            _insert_pool.release_node(interthread_task);
        }

        coroutines::promise_touch_result touch_result = coroutines::promise_touch_result::e_executed;
        task current_task;
        int old_total_quants;
        std::chrono::steady_clock::time_point start_time;

        // NOTE: Taking nextup node or pulling it from a pool
        if (_nextup) [[likely]] {
            current_task = std::move(_nextup.value());
            _nextup.reset();
        } else if (not _pool.empty()) [[unlikely]] {
            current_task = std::move(_pool.front());
            _pool.pop();
        } else {
            return false;
        }

        // NOTE: Pulling next task and prefetching it
        if (not _pool.empty()) [[likely]] {
            _nextup = std::move(_pool.front());
            _pool.pop();
            prefetch<e_l1_cache>(_nextup.value()._coroutine.address());
        }

        // NOTE: Starting quants counter and removing old quants amount
        if (_common_quants) {
            start_time = std::chrono::steady_clock::now();
            old_total_quants = _total_quants;
            _total_quants -= current_task._coroutine.promise()._quants.value();
        }

        // NOTE: Proceeding context
        current_task.awake(&touch_result);

        // NOTE: Checking if context can be resumed
        const bool is_resumable {
                current_task
            and touch_result not_eq coroutines::promise_touch_result::e_failed
            and touch_result not_eq coroutines::promise_touch_result::e_finished
            and touch_result not_eq coroutines::promise_touch_result::e_detached
        };

        // TODO: Separate pool onto incoming pool (for task rescheduling) and processing pool (for task processing)

        // NOTE: Checking if the context shall be forwarded via passed conductor
        const bool is_conducted {
            is_resumable
            and current_task._coroutine.promise()._runner_conductor
        };

        // NOTE: Decision if node shall be released or pushed back
        const bool is_idle = not is_resumable or is_conducted;

        // NOTE: Updating global total counter
        if (_common_quants) {
            // NOTE: Increasing quants because task is resumable
            if (is_resumable)
                _total_quants += current_task._coroutine.promise()._quants.add(
                static_cast<int>((std::chrono::steady_clock::now() - start_time).count()));
            _common_quants->fetch_add(_total_quants, std::memory_order_relaxed);
            _common_quants->fetch_sub(old_total_quants, std::memory_order_relaxed);
        }

        // NOTE: Forwarding via conductor if needed
        if (is_conducted) [[likely]]
            current_task._coroutine.promise()._runner_conductor->forward(std::forward<task>(current_task));

        // NOTE: If task is idle, releasing it's node. Else returning it back to the local pool
        if (not is_idle) _pool.push(std::move(current_task));

        return true;
    }

    /**
     * @brief Ejects task from runner
     * @return Optional of ejected task
     */
    std::optional<task> eject() const noexcept {
        if (_pool.empty()) [[unlikely]] return std::nullopt;
        auto ret_task = std::move(_pool.front());
        _pool.pop();
        return ret_task;
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
