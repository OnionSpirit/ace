/**
 * @file
 * @details This file contains a @b runner class, that executes and controls passed tasks
 */
#ifndef ACE_RUNNER_H
#define ACE_RUNNER_H

#include <queue>
#include <chrono>
#include <nukes/dynamic/mpsc_queue.h>
#include <nukes/details/prefetch.h>

#include "ace/core/tools/moving_average.h"
#include "ace/core/tools/macro.h"
#include "ace/core/async.h"


namespace ace::core {

    /**
     * @details coroutines execution manager.
     * @tparam InitialSize Initial size of Pools
     * @tparam AllocationPolicy Pools allocation policy
     * @tparam Policies Pools policies, each policy provides independent
     * pool for coroutines
     */
    struct runner {

        typedef nukes::dynamic::mpsc_queue<task> insert_pool_t;

        typedef runner_pool_t::node_t *pool_node_ptr;
        typedef insert_pool_t::node_t *insert_node_ptr;

        ACE_CACHE_LINE(0)

        mutable runner_pool_t        _pool{}; ///< Pool of the assigned tasks

        ACE_CACHE_LINE(1)

        mutable nukes::dynamic::mpsc_queue<task> _interthread_pool{}; ///< Pool for the interthread insertion

        ACE_CACHE_LINE(4)

        tools::moving_average       _quants       {}; ///< Average amount of the time quants for the run operation call
        long                        _tasks_amount {};
        runner_pool_t               _vortex_pool  {};

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
         * @param ctx Task to be reattached safely into @c runner
         */
        static void threadsafe_reattach(task &&ctx);

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into @c runner
         *
         * @warning @b NOT @b THREADSAFE
         */
        static void reattach(insert_node_ptr& node);

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into @c runner
         *
         * @warning @b NOT @b THREADSAFE
         */
        static void reattach(pool_node_ptr& node);

        /**
         * @brief Returns task node safely into source @c runner
         * @param node Task node to be reattached into @c runner
         */
        static void threadsafe_reattach(insert_node_ptr& node);

        /**
         * @brief Returns task node safely into source @c runner
         * @param node Task node to be reattached into @c runner
         */
        static void threadsafe_reattach(pool_node_ptr& node);

        /**
         * @details Calculates runner's velocity
         * @return Velocity value
         */
        double velocity() const noexcept;

        /**
         * @details Clears runner's velocity
         */
        void clear_velocity() noexcept { _quants.clear(); }

        /**
         * @details Calculates runner's velocity
         * @param interval Interval to add to time spent moving average
         * @return Velocity value
         */
        template<typename Rep, typename Period>
        double upgrade_velocity(std::chrono::duration<Rep, Period> interval) noexcept {
            return static_cast<double>(_tasks_amount) / static_cast<double>(_quants.add(interval.count()));
        }

        /**
         * @details Resumes only one ready task
         * @return @b true if task was processed, @b false otherwise
         */
        bool yank() noexcept;

        /**
         * @details Resumes only vortex service tasks
         * @return @b true if task was processed, @b false otherwise
         */
        bool yank_vortex() noexcept;

        /**
         * @details Checks if runner has only vortex polling tasks
         * @return @c true if runner has only vortex tasks, @c false otherwise
         */
        bool is_polling() const noexcept {
            return _pool.empty() and not _vortex_pool.empty();
        };

        /**
         * @brief Ejects task from runner
         * @return Optional of ejected task
         */
        std::optional<task> eject() noexcept;

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
        template <typename async_return_t, typename async_rule_t>
        void attach(async<async_return_t, async_rule_t> &&new_task) noexcept {
            ++_tasks_amount;
            new_task._coroutine.promise()._runner_pool = &_pool;
            _interthread_pool.push(std::forward<task>(task_wrap(
                std::forward<core::async<async_return_t, async_rule_t> >(new_task))));
        }

        /**
         * @details Function to attach task to the runner
         * @param new_task Task to be pushed into the runner
         * @warning NOT THREADSAFE
         * @return void
         */
        template <typename async_return_t, typename async_rule_t>
        void attach_front(async<async_return_t, async_rule_t> &&new_task) noexcept {
            ++_tasks_amount;
            new_task._coroutine.promise()._runner_pool = &_pool;
            if (new_task._coroutine.promise()._polling) {
                _vortex_pool.push_front(std::forward<task>(task_wrap(
                    std::forward<async<async_return_t, async_rule_t> >(new_task))));
            } else {
                _pool.push_front(std::forward<task>(task_wrap(
                    std::forward<async<async_return_t, async_rule_t> >(new_task))));
            }
        }

        /**
         * @details Checks if any Tasks stored in the runner
         * @return @b true if empty, @b false otherwise
         */
        [[nodiscard]] bool empty() const noexcept {
            return _pool.empty() and _vortex_pool.empty() and _interthread_pool.empty();
        };
    };

    template<>
    inline void runner::attach<void, differed>(task &&new_task) noexcept {
        ++_tasks_amount;
        new_task._coroutine.promise()._runner_pool = &_pool;
        _interthread_pool.push(std::forward<task>(new_task));
    }

    template<>
    inline void runner::attach_front<void, differed>(task &&new_task) noexcept {
        ++_tasks_amount;
        new_task._coroutine.promise()._runner_pool = &_pool;
        if (new_task._coroutine.promise()._polling) {
            _vortex_pool.push_front(std::forward<task>(new_task));
        } else {
            _pool.push_front(std::forward<task>(new_task));
        }
    }

    inline auto pool_to_runner(runner_pool_t *pool) noexcept {
        return reinterpret_cast<runner *>(pool);
    }

    inline runner::runner(runner &&t) noexcept {
        this->_pool = std::move(t._pool);
        this->_quants = std::move(t._quants);
        this->_interthread_pool = std::move(t._interthread_pool);
        this->_tasks_amount = t._tasks_amount;
        t._tasks_amount = 0;
    };

    inline runner& runner::operator=(runner &&t) noexcept {
        this->_pool = std::move(t._pool);
        this->_quants = std::move(t._quants);
        this->_interthread_pool = std::move(t._interthread_pool);
        this->_tasks_amount = t._tasks_amount;
        t._tasks_amount = 0;
        return *this;
    };


    inline void runner::reattach(task&& ctx) {
        if (not ctx.is_exist() or not ctx._coroutine.promise()._runner_pool)
            return;
        if (ctx._coroutine.promise()._polling)
            pool_to_runner(ctx._coroutine.promise()._runner_pool)->_vortex_pool.push(std::move(ctx));
        else
            ctx._coroutine.promise()._runner_pool->push(std::move(ctx));
    }


    inline void runner::threadsafe_reattach(task&& ctx) {
        if (not ctx.is_exist() or not ctx._coroutine.promise()._runner_pool)
            return;
        pool_to_runner(ctx._coroutine.promise()._runner_pool)->_interthread_pool.push(std::move(ctx));
    }


    inline void runner::reattach(insert_node_ptr& node) {
        if (not node or not node->_data.is_exist() or not node->_data._coroutine.promise()._runner_pool)
            return;
        auto n = nukes::details::nodes::cast_node(node);
        if (node->_data._coroutine.promise()._polling)
            pool_to_runner(node->_data._coroutine.promise()._runner_pool)->_vortex_pool.push_node(n);
        else
            pool_to_runner(node->_data._coroutine.promise()._runner_pool)->_pool.push_node(n);
        node = nullptr;
    }


    inline void runner::reattach(pool_node_ptr& node) {
        if (not node or not node->_data.is_exist() or not node->_data._coroutine.promise()._runner_pool)
            return;
        if (node->_data._coroutine.promise()._polling)
            pool_to_runner(node->_data._coroutine.promise()._runner_pool)->_vortex_pool.push_node(node);
        else
            pool_to_runner(node->_data._coroutine.promise()._runner_pool)->_pool.push_node(node);
        node = nullptr;
    }


    inline void runner::threadsafe_reattach(insert_node_ptr& node) {
        if (not node or not node->_data.is_exist() or not node->_data._coroutine.promise()._runner_pool)
            return;
        pool_to_runner(node->_data._coroutine.promise()._runner_pool)->_interthread_pool.push_node(node);
        node = nullptr;
    }


    inline void runner::threadsafe_reattach(pool_node_ptr& node) {
        if (not node or not node->_data.is_exist() or not node->_data._coroutine.promise()._runner_pool)
            return;
        auto n = nukes::details::nodes::cast_node(node);
        pool_to_runner(node->_data._coroutine.promise()._runner_pool)->_interthread_pool.push_node(n);
        node = nullptr;
    }


    inline double runner::velocity() const noexcept {
        if (_quants.value() == 0) [[unlikely]] return 0.0;
        return static_cast<double>(_tasks_amount) / static_cast<double>(_quants.value());
    }


    inline bool runner::yank() noexcept {

        promise_touch_result touch_result = e_executed;
        pool_node_ptr task_node = _pool.pop_node();

        // NOTE: Pulling from interthread pool if task is empty
        if (not task_node) [[unlikely]] {
            if (const auto interthread_node = _interthread_pool.pop_node())
                task_node = cast_node(interthread_node);
            else return false;
        }

        // NOTE: Prefetching next task frame
        if (const auto head = _pool.inspect_head()) [[likely]] {
            head->_data.prefetch();
        }

        // NOTE: Proceeding async
        task_node->_data.awake(&touch_result);

        // NOTE: Checking if async can be resumed
        const bool is_resumable {
            task_node->_data
            and touch_result not_eq e_failed
            and touch_result not_eq e_finished
            and touch_result not_eq e_detached
        };

        // NOTE: Checking if the async shall be forwarded via passed conductor
        const bool is_conducted {
            is_resumable
            and task_node->_data._coroutine.promise()._runner_conductor
        };

        // NOTE: Decision if node shall be released or pushed back
        const bool is_idle = not is_resumable or is_conducted;

        // NOTE: Forwarding via conductor if needed
        if (is_conducted) [[likely]]
            task_node = task_node->_data._coroutine.promise()._runner_conductor->forward_node(task_node);

        if (not is_resumable) [[unlikely]] --_tasks_amount;

        // NOTE: If task is idle, releasing it's node. Else returning it back to the local pool
        if (is_idle and task_node) _pool.release_node(task_node);
        else if (task_node and not task_node->_data._coroutine.promise()._polling) _pool.push_node(task_node);
        else if (task_node) _vortex_pool.push_node(task_node);

        return true;
    }


    inline bool runner::yank_vortex() noexcept {

        promise_touch_result touch_result = e_executed;
        pool_node_ptr vortex_node = _vortex_pool.pop_node();

        // NOTE: If node is empty breaking
        if (not vortex_node) [[unlikely]] return false;

        // NOTE: Proceeding async
        vortex_node->_data.awake(&touch_result);

        // NOTE: Checking if async can be resumed
        const bool is_resumable {
            vortex_node->_data
            and touch_result not_eq e_failed
            and touch_result not_eq e_finished
            and touch_result not_eq e_detached
        };

        // NOTE: Checking if the async shall be forwarded via passed conductor
        const bool is_conducted {
            is_resumable
            and vortex_node->_data._coroutine.promise()._runner_conductor
        };

        // NOTE: Decision if node shall be released or pushed back
        const bool is_idle = not is_resumable or is_conducted;

        // NOTE: Forwarding via conductor if needed
        if (is_conducted) [[likely]]
            vortex_node = vortex_node->_data._coroutine.promise()._runner_conductor->forward_node(vortex_node);

        if (not is_resumable) [[unlikely]] --_tasks_amount;

        // NOTE: If task is idle, releasing it's node. Else returning it back to the local pool
        if (is_idle and vortex_node) _vortex_pool.release_node(vortex_node);
        else if (vortex_node and vortex_node->_data._coroutine.promise()._polling) _vortex_pool.push_node(vortex_node);
        else if (vortex_node) _pool.push_node(vortex_node);

        return true;
    }


    inline std::optional<task> runner::eject() noexcept {
        if (task ejective; _pool.pop(ejective)) [[likely]] {
            --_tasks_amount;
            return ejective;
        }
        return std::nullopt;
    }


    inline bool runner::run() noexcept {
        int i = 0;
        for (constexpr int yank_limit = 128; i < yank_limit and yank(); ++i) {
            if (i % 16 == 0) {
                yank_vortex();
                insert_node_ptr interthread_node;
                // TODO: Use batch pop instead of the loop
                while ((interthread_node = _interthread_pool.pop_node())) {
                    // NOTE: Fetching task from interthread insert queue
                    auto placing_node = cast_node(interthread_node);
                    _pool.push_node(placing_node);
                }
            }
        }
        return i not_eq 0 or yank_vortex();
    }

} // end namespace ace::core


//==============================DEFINITIONS==================================

#undef ACE_RUNNER_META
#undef ACE_RUNNER_MEMBER
#endif // ACE_RUNNER_H
