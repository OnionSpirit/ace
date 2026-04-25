/**
 * @file
 * @details This file contains a @b runner class, that executes and controls passed tasks
 */
#ifndef ACE_RUNNER_H
#define ACE_RUNNER_H

#include <queue>
#include <chrono>
#include <nukes/dynamic/mpsc_queue.h>

#include "ace/core/misc/prefetch.h"
#include "ace/core/misc/terms.h"
#include "ace/core/context.h"


namespace ace::core {

    /**
     * @details coroutines execution manager.
     * @tparam InitialSize Initial size of Pools
     * @tparam AllocationPolicy Pools allocation policy
     * @tparam Policies Pools policies, each policy provides independent
     * pool for coroutines
     */
    struct runner {

        ACE_CACHE_LINE(0)

        typedef nukes::dynamic::mpsc_queue<task> insert_pool_t;

        typedef runner_pool_t::node_t *pool_node_ptr;
        typedef insert_pool_t::node_t *insert_node_ptr;

        mutable runner_pool_t _pool{}; ///< Pool of the assigned tasks
        std::optional<pool_node_ptr> _nextup{}; ///< Nextup task for running

        ACE_CACHE_LINE(1)

        mutable nukes::dynamic::mpsc_queue<task> _interthread_pool{}; ///< Pool for the interthread insertion

        ACE_CACHE_LINE(4)

        std::atomic<int> *_common_quants{}; ///< Pointer to common quant counter
        int _total_quants{}; ///< Total amount of the time quants from the all tasks on a pool

        runner() = default;

        // TODO: Need to figure out how to validate this wo warn cuz its important
        // {
        //     static_assert(offsetof(runner, _pool) == 0,
        //         "'_pool' must be the first member of runner. Stop touching not your code idiot");
        // };

        ~runner() = default;

        runner(runner &&t) noexcept;

        runner &operator=(runner &&t) noexcept;

        /**
         * @brief Returns task into source @c runner
         * @param ctx Task to be reattached into @c runner
         *
         * @warning @b NOT @b THREADSAFE
         */
        static void reattach(task &&ctx);

        /**
         * @brief Returns task into source @c runner
         * @param ctx Task to be reattached into @c runner
         */
        static void threadsafe_reattach(task &&ctx);

        /**
         * @details Resumes only one ready task
         * @return @b true if task was processed, @b false otherwise
         */
        bool yank() noexcept;

        /**
         * @brief Ejects task from runner
         * @return Optional of ejected task
         */
        std::optional<task> eject() const noexcept;

        /**
         * @details Resumes tasks from the ready task pool until it is empty, or limit (1024) reached.
         * @return @b true if runner made some tasks, @b false otherwise
         */
        bool run() noexcept;

        /**
         * @details Function to attach task to the runner
         * @param new_task Task to be pushed into the runner
         * @return void
         */
        template<typename async_return_t>
        void attach(async<async_return_t> &&new_task) const noexcept {
            new_task._coroutine.promise()._runner_pool = &_pool;
            _pool.push(std::forward<task>(async_wrap(std::forward<async<async_return_t> >(new_task))));
        }

        /**
         * @details Checks if any Tasks stored in the runner
         * @return @b true if empty, @b false otherwise
         */
        [[nodiscard]] bool empty() const noexcept { return _pool.empty() and _interthread_pool.empty(); };
    };

    template<>
    inline void runner::attach<void>(task &&new_task) const noexcept {
        new_task._coroutine.promise()._runner_pool = &_pool;
        _pool.push(std::forward<task>(new_task));
    }

    inline auto pool_to_runner(runner_pool_t *pool) noexcept {
        return reinterpret_cast<runner *>(pool);
    }

    inline runner::runner(runner &&t) noexcept {
        this->_pool = std::move(t._pool);
        this->_nextup = t._nextup;
        t._nextup = std::nullopt;
        this->_common_quants = t._common_quants;
        t._common_quants = nullptr;
        this->_total_quants = t._total_quants;
        t._total_quants = 0;
        this->_interthread_pool = std::move(t._interthread_pool);
    };

    inline runner& runner::operator=(runner &&t) noexcept {
        this->_pool = std::move(t._pool);
        this->_nextup = t._nextup;
        t._nextup = std::nullopt;
        this->_common_quants = t._common_quants;
        t._common_quants = nullptr;
        this->_total_quants = t._total_quants;
        t._total_quants = 0;
        this->_interthread_pool = std::move(t._interthread_pool);
        return *this;
    };


    inline void runner::reattach(task &&ctx) {
        if (not ctx.is_resumable() or not ctx._coroutine.promise()._runner_pool)
            return;
        ctx._coroutine.promise()._runner_pool->push(std::move(ctx));
    }


    inline void runner::threadsafe_reattach(task &&ctx) {
        if (not ctx.is_resumable() or not ctx._coroutine.promise()._runner_pool)
            return;
        pool_to_runner(ctx._coroutine.promise()._runner_pool)->_interthread_pool.push(std::move(ctx));
    }


    inline bool runner::yank() noexcept {
        core::promise_touch_result touch_result = core::promise_touch_result::e_executed;
        pool_node_ptr task_node;
        int old_total_quants;
        std::chrono::steady_clock::time_point start_time;

        // NOTE: Taking nextup node if it is exists
        if (_nextup) [[likely]] {
            task_node = _nextup.value();
            _nextup.reset();
        }
        // NOTE: Pulling task from a pool if there is not nextup task
        else if (not _pool.empty()) [[unlikely]] {
            task_node = _pool.pop_node();
        }
        // NOTE: Fetching task from the interthread queue if pool is empty
        else if (const auto interthread_node = _interthread_pool.pop_node(); interthread_node) {
            const auto placing_node = cast_node(interthread_node);
            task_node = placing_node;
        } else return false;

        // NOTE: Pulling next task and prefetching it
        if (not _pool.empty()) [[likely]] {
            _nextup = _pool.pop_node();
            misc::prefetch<misc::e_l1_cache>(_nextup.value()->_data._coroutine.address());
        }

        // NOTE: Starting quants counter and removing old quants amount
        if (_common_quants) {
            start_time = std::chrono::steady_clock::now();
            old_total_quants = _total_quants;
            _total_quants -= task_node->_data._coroutine.promise()._quants.value();
        }

        // NOTE: Proceeding context
        task_node->_data.awake(&touch_result);

        // NOTE: Checking if context can be resumed
        const bool is_resumable{
            task_node->_data
            and touch_result not_eq core::promise_touch_result::e_failed
            and touch_result not_eq core::promise_touch_result::e_finished
            and touch_result not_eq core::promise_touch_result::e_detached
        };

        // NOTE: Checking if the context shall be forwarded via passed conductor
        const bool is_conducted{
            is_resumable
            and task_node->_data._coroutine.promise()._runner_conductor
        };

        // NOTE: Decision if node shall be released or pushed back
        const bool is_idle = not is_resumable or is_conducted;

        // NOTE: Updating global total counter
        if (_common_quants) {
            // NOTE: Increasing quants because task is resumable
            if (is_resumable)
                _total_quants += task_node->_data._coroutine.promise()._quants.add(
                    static_cast<int>((std::chrono::steady_clock::now() - start_time).count()));
            _common_quants->fetch_add(_total_quants, std::memory_order_relaxed);
            _common_quants->fetch_sub(old_total_quants, std::memory_order_relaxed);
        }

        // NOTE: Forwarding via conductor if needed
        if (is_conducted) [[likely]]
                task_node->_data._coroutine.promise()._runner_conductor->forward(std::forward<task>(task_node->_data));

        // NOTE: If task is idle, releasing it's node. Else returning it back to the local pool
        if (is_idle) _pool.release_node(task_node);
        else _pool.push_node(task_node);

        return true;
    }


    inline std::optional<task> runner::eject() const noexcept {
        if (task ejective; _pool.pop(ejective)) [[likely]]
                return ejective;
        return std::nullopt;
    }


    inline bool runner::run() noexcept {
        int i = 0;
        for (constexpr int yank_limit = 128; i < yank_limit and yank(); ++i) {
            if (i % 16 == 0) {
                insert_node_ptr interthread_node;
                // TODO: Use batch pop instead of the loop
                while ((interthread_node = _interthread_pool.pop_node())) {
                    // NOTE: Fetching task from interthread insert queue
                    auto placing_node = cast_node(interthread_node);
                    _pool.push_node(placing_node);
                }
            }
        }
        return i not_eq 0;
    }

} // end namespace ace::core


//==============================DEFINITIONS==================================

#undef ACE_RUNNER_META
#undef ACE_RUNNER_MEMBER
#endif // ACE_RUNNER_H
