/**
 * @file
 * @details This file contains a @b runner class, that executes and controls passed tasks
 */
#ifndef ACE_RUNNER_H
#define ACE_RUNNER_H

#include <queue>
#include <chrono>
#include <nukes/dynamic/mpsc_queue.h>

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

        static thread_local ace::core::runner* current_runner_ptr;

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
         * @brief Defines current active runner on the current thread
         * @warning Returns nullptr if @c runner::run() is not in action
         * @return This thread runner ptr
         */
        static runner* get_runner() { return current_runner_ptr; }

        static runner* pool_to_runner(runner_pool_t *pool) noexcept;

        static runner_pool_t* runner_to_pool(runner* rnr) noexcept;

        /**
         * @brief Returns task into source @c runner
         * @param ctx Task to be reattached into @c runner
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach(task &&ctx, const runner* local_runner_ptr = current_runner_ptr);

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into @c runner
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach(pool_node_ptr& node, const runner* local_runner_ptr = current_runner_ptr);

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into @c runner
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach(insert_node_ptr& node, const runner* local_runner_ptr = current_runner_ptr);

        /**
         * @brief Returns task into source @c runner
         * @param ctx Task to be reattached into @c runner
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach_front(task &&ctx, const runner* local_runner_ptr = current_runner_ptr);

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into @c runner
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach_front(pool_node_ptr& node, const runner* local_runner_ptr = current_runner_ptr);

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into @c runner
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach_front(insert_node_ptr& node, const runner* local_runner_ptr = current_runner_ptr);

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
        void attach(async<async_return_t, async_rule_t> &&new_task) noexcept;

        /**
         * @details Function to attach task to the runner
         * @param new_task Task to be pushed into the runner
         * @return void
         */
        template <typename async_return_t, typename async_rule_t>
        void attach_front(async<async_return_t, async_rule_t> &&new_task) noexcept;

        /**
         * @details Checks if any Tasks stored in the runner
         * @return @b true if empty, @b false otherwise
         */
        [[nodiscard]] bool empty() const noexcept {
            return _pool.empty() and _vortex_pool.empty() and _interthread_pool.empty();
        };
    };

    inline runner* runner::pool_to_runner(runner_pool_t *pool) noexcept {
        return reinterpret_cast<runner *>(pool);
    }

    inline runner_pool_t* runner::runner_to_pool(runner* rnr) noexcept {
        return reinterpret_cast<runner_pool_t*>(rnr);
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


    inline void runner::reattach(task&& ctx, const runner* local_runner_ptr) {
        const auto* target_runner_ptr = ctx._coroutine.promise()._runner.as<runner>();
        if (not ctx.is_exist()) [[unlikely]]
            throw std::runtime_error { "trying to 'reattach' idle context" };
        if (not target_runner_ptr or not local_runner_ptr) [[unlikely]]
            throw std::logic_error {
                "'reattach' operation can't be applied to 'ace::core::async<...>'s "
                "which are not running at the 'ace::core::runner'"
            };
        if (local_runner_ptr == target_runner_ptr)
            local_runner_ptr->_pool.push(std::move(ctx));
        else
            target_runner_ptr->_interthread_pool.push(std::move(ctx));
    }


    inline void runner::reattach(pool_node_ptr& node, const runner* local_runner_ptr) {
        const auto* target_runner_ptr = node->_data._coroutine.promise()._runner.as<runner>();
        if (not node or not node->_data.is_exist()) [[unlikely]]
            throw std::runtime_error { "trying to 'reattach' idle context" };
        if (not target_runner_ptr or not local_runner_ptr) [[unlikely]]
            throw std::logic_error {
                "'reattach' operation can't be applied to 'ace::core::async<...>'s "
                "which are not running at the 'ace::core::runner'"
            };
        if (local_runner_ptr == target_runner_ptr) {
            local_runner_ptr->_pool.push_node(node);
            node = nullptr;
        } else {
            auto* n = nukes::details::nodes::cast_node(node);
            target_runner_ptr->_interthread_pool.push_node(n);
            node = nullptr;
        }
    }


    inline void runner::reattach(insert_node_ptr& node, const runner* local_runner_ptr) {
        const auto* target_runner_ptr = node->_data._coroutine.promise()._runner.as<runner>();
        if (not node or not node->_data.is_exist()) [[unlikely]]
            throw std::runtime_error { "trying to 'reattach' idle context" };
        if (not target_runner_ptr or not local_runner_ptr) [[unlikely]]
            throw std::logic_error {
                "'reattach' operation can't be applied to 'ace::core::async<...>'s "
                "which are not running at the 'ace::core::runner'"
            };
        if (local_runner_ptr == target_runner_ptr) {
            auto* n = nukes::details::nodes::cast_node(node);
            local_runner_ptr->_pool.push_node(n);
            node = nullptr;
        } else {
            target_runner_ptr->_interthread_pool.push_node(node);
            node = nullptr;
        }
    }


    inline void runner::reattach_front(task&& ctx, const runner* local_runner_ptr) {
        const auto* target_runner_ptr = ctx._coroutine.promise()._runner.as<runner>();
        if (not ctx.is_exist()) [[unlikely]]
            throw std::runtime_error { "trying to 'reattach_front' idle context" };
        if (not target_runner_ptr or not local_runner_ptr) [[unlikely]]
            throw std::logic_error {
                "'reattach_front' operation can't be applied to 'ace::core::async<...>'s "
                "which are not running at the 'ace::core::runner'"
            };
        if (local_runner_ptr == target_runner_ptr) {
            ctx.prefetch();
            local_runner_ptr->_pool.push_front(std::move(ctx));
        } else
            target_runner_ptr->_interthread_pool.push(std::move(ctx));
    }


    inline void runner::reattach_front(pool_node_ptr& node, const runner* local_runner_ptr) {
        const auto* target_runner_ptr = node->_data._coroutine.promise()._runner.as<runner>();
        if (not node or not node->_data.is_exist()) [[unlikely]]
            throw std::runtime_error { "trying to 'reattach_front' idle context" };
        if (not target_runner_ptr or not local_runner_ptr) [[unlikely]]
            throw std::logic_error {
                "'reattach_front' operation can't be applied to 'ace::core::async<...>'s "
                "which are not running at the 'ace::core::runner'"
            };
        if (local_runner_ptr == target_runner_ptr) {
            node->_data.prefetch();
            local_runner_ptr->_pool.push_node_front(node);
            node = nullptr;
        } else {
            auto* n = nukes::details::nodes::cast_node(node);
            target_runner_ptr->_interthread_pool.push_node(n);
            node = nullptr;
        }
    }


    inline void runner::reattach_front(insert_node_ptr& node, const runner* local_runner_ptr) {
        const auto* target_runner_ptr = node->_data._coroutine.promise()._runner.as<runner>();
        if (not node or not node->_data.is_exist()) [[unlikely]]
            throw std::runtime_error { "trying to 'reattach_front' idle context" };
        if (not target_runner_ptr or not local_runner_ptr) [[unlikely]]
            throw std::logic_error {
                "'reattach_front' operation can't be applied to 'ace::core::async<...>'s "
                "which are not running at the 'ace::core::runner'"
            };
        if (local_runner_ptr == target_runner_ptr) {
            node->_data.prefetch();
            auto* n = nukes::details::nodes::cast_node(node);
            local_runner_ptr->_pool.push_node_front(n);
            node = nullptr;
        } else {
            target_runner_ptr->_interthread_pool.push_node(node);
            node = nullptr;
        }
    }


    template <typename async_return_t, typename async_rule_t>
    void runner::attach(async<async_return_t, async_rule_t> &&new_task) noexcept {
        ++_tasks_amount;
        new_task._coroutine.promise()._runner = &_pool;
        reattach(std::move(new_task), this);
    }


    template <typename async_return_t, typename async_rule_t>
    void runner::attach_front(async<async_return_t, async_rule_t> &&new_task) noexcept {
        ++_tasks_amount;
        new_task._coroutine.promise()._runner = &_pool;
        reattach_front(std::move(new_task), pool_to_runner(&_pool));
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
            // NOTE: If there is no regular tasks then processing services
            else return yank_vortex();
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
        current_runner_ptr = this;
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
        current_runner_ptr = nullptr;
        return i not_eq 0 or yank_vortex();
    }

} // end namespace ace::core


thread_local ace::core::runner* ace::core::runner::current_runner_ptr = nullptr;

template<typename returnT, ace::core::is_promise_rule promise_rule_t>
inline auto ace::core::async<returnT, promise_rule_t>::get_current_pool() noexcept
-> ace::core::async<returnT, promise_rule_t>::runner_pool_t* {
    return runner::runner_to_pool(runner::get_runner());
}

//==============================DEFINITIONS==================================

#undef ACE_RUNNER_META
#undef ACE_RUNNER_MEMBER
#endif // ACE_RUNNER_H
