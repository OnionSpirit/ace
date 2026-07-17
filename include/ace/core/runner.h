/**
 * @file runner.h
 * @brief Per-thread task runner — the core execution unit of the ACE runtime.
 *
 * @details Each @c runner owns a lock-free MPSC task queue (@c _pool), an
 * inter-thread insertion queue (@c _interthread_pool), and a vortex service
 * pool (@c _vortex_pool).  The dispatcher assigns tasks to runners via
 * @c attach() / @c attach_front().
 *
 * ### Execution loop
 *
 * @c run() processes up to 128 tasks per call (the @c yank_limit).  Every
 * 16 tasks it drains the @c _interthread_pool and processes vortex tasks.
 *
 * ### Task lifecycle inside a runner
 *
 * 1. @c attach()/@c attach_front() — sets @c _runner_pool on the promise
 *    and enqueues the task.
 * 2. @c yank() — pops a task, calls @c awake(), and decides whether to
 *    re-queue, release, or forward via router.
 * 3. @c reattach() — returns a task to its owning runner (used by futures
 *    to wake a suspended coroutine).
 *
 * @see ace::core::dispatcher, ace::core::async
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
     * @brief Per-thread coroutine execution manager.
     *
     * @details Owns three task queues:
     *  - @c _pool — lock-free MPSC queue for local tasks (fast path).
     *  - @c _interthread_pool — lock-free MPSC queue for cross-thread inserts.
     *  - @c _vortex_pool — queue for low-priority polling tasks (vortex services).
     *
     * Tracks @c _tasks_amount for velocity calculation (used by the balancer
     * for weighted task distribution).
     */
    struct runner {

        typedef nukes::dynamic::mpsc_queue<task> insert_pool_t;

        typedef runner_pool_t::node_t *pool_node_ptr;
        typedef insert_pool_t::node_t *insert_node_ptr;

        enum class pull_source : uint8_t {
            e_local_pool,
            e_interthread_pool,
        };

        ACE_CACHE_LINE(0)

        mutable runner_pool_t           _pool               {}; ///< Pool of the assigned tasks
        tools::moving_average           _quants             {}; ///< Average amount of the time quants for the run operation call

        ACE_CACHE_LINE(1)

        runner_pool_t                   _vortex_pool        {};
        long                            _tasks_amount       {};
        pull_source                     _pull_source        { pull_source::e_local_pool };

        ACE_CACHE_LINE(2)

        mutable insert_pool_t           _insert_pool   {}; ///< Pool for the interthread insertion

        static thread_local omni_runner current_runner_ptr;

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
        static omni_runner get() { return current_runner_ptr; }

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into @c runner
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach(omni_node& node, const omni_runner local_runner_ptr = current_runner_ptr);

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into @c runner
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach(omni_node&& node, const omni_runner local_runner_ptr = current_runner_ptr);

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into a front of the @c runner pool
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach_front(omni_node& node, const omni_runner local_runner_ptr = current_runner_ptr);

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into a front of the @c runner pool
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach_front(omni_node&& node, const omni_runner local_runner_ptr = current_runner_ptr);

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
        void attach(async<async_return_t, async_rule_t> &&new_task);

        /**
         * @details Function to attach task to the runner
         * @param new_task Task to be pushed into the runner
         * @return void
         */
        template <typename async_return_t, typename async_rule_t>
        void attach_front(async<async_return_t, async_rule_t> &&new_task);

        /**
         * @details Checks if any Tasks stored in the runner
         * @return @b true if empty, @b false otherwise
         */
        [[nodiscard]] bool empty() const noexcept {
            return _pool.empty() and _vortex_pool.empty() and _insert_pool.empty();
        };

        omni_node fetch_task_node();

    private:

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into @c runner
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach_impl(omni_node& node, const omni_runner local_runner_ptr);

        /**
         * @brief Returns task node into source @c runner
         * @param node Task node to be reattached into a front of the @c runner pool
         * @param local_runner_ptr Runner that requests reattach operation
         */
        static void reattach_front_impl(omni_node& node, const omni_runner local_runner_ptr);

    };


    inline runner::runner(runner &&t) noexcept {
        this->_pool = std::move(t._pool);
        this->_quants = std::move(t._quants);
        this->_insert_pool = std::move(t._insert_pool);
        this->_tasks_amount = t._tasks_amount;
        t._tasks_amount = 0;
    };

    inline runner& runner::operator=(runner &&t) noexcept {
        this->_pool = std::move(t._pool);
        this->_quants = std::move(t._quants);
        this->_insert_pool = std::move(t._insert_pool);
        this->_tasks_amount = t._tasks_amount;
        t._tasks_amount = 0;
        return *this;
    };


    inline void runner::reattach_impl(omni_node& node, const omni_runner local_runner_ptr) {
        using namespace nukes::details::nodes;
        if (not node or not node->_data.is_exist()) [[unlikely]]
            throw std::runtime_error { "trying to 'reattach' idle context" };
        const omni_runner target_runner_ptr = node->_data._coroutine.promise()._runner;
        if (not target_runner_ptr or not local_runner_ptr) [[unlikely]]
            throw std::logic_error {
                "'reattach' operation can't be applied to 'ace::core::async<...>'s "
                "which are not running at the 'ace::core::runner'"
            };
        if (local_runner_ptr == target_runner_ptr)
            local_runner_ptr->_pool.push_node(node);
        else
            target_runner_ptr->_insert_pool.push_node(node);
    }


    inline void runner::reattach_front_impl(omni_node& node, const omni_runner local_runner_ptr) {
        using namespace nukes::details::nodes;
        if (not node or not node->_data.is_exist()) [[unlikely]]
            throw std::runtime_error { "trying to 'reattach_front' idle context" };
        const omni_runner target_runner_ptr = node->_data._coroutine.promise()._runner;
        if (not target_runner_ptr or not local_runner_ptr) [[unlikely]]
            throw std::logic_error {
                "'reattach_front' operation can't be applied to 'ace::core::async<...>'s "
                "which are not running at the 'ace::core::runner'"
            };
        if (local_runner_ptr == target_runner_ptr) {
            node->_data.prefetch();
            local_runner_ptr->_pool.push_node_front(node);
        } else
            target_runner_ptr->_insert_pool.push_node(node);
    }


    inline void runner::reattach(omni_node& node, const omni_runner local_runner_ptr) {
        reattach_impl(node, local_runner_ptr);
        node.reset();
    }

    inline void runner::reattach(omni_node&& node, const omni_runner local_runner_ptr) {
        reattach_impl(node, local_runner_ptr);
    }

    inline void runner::reattach_front(omni_node& node, const omni_runner local_runner_ptr) {
        reattach_front_impl(node, local_runner_ptr);
        node.reset();
    }

    inline void runner::reattach_front(omni_node&& node, const omni_runner local_runner_ptr) {
        reattach_front_impl(node, local_runner_ptr);
    }


    template <typename async_return_t, typename async_rule_t>
    void runner::attach(async<async_return_t, async_rule_t> &&new_task) {
        ++_tasks_amount;
        new_task._coroutine.promise()._runner = &_pool;
        if (insert_node_ptr new_node; _insert_pool._mempool.capture(new_node)) {
            new_node->_data = std::move(new_task);
            reattach(new_node, this);
        }
    }


    template <typename async_return_t, typename async_rule_t>
    void runner::attach_front(async<async_return_t, async_rule_t> &&new_task) {
        ++_tasks_amount;
        new_task._coroutine.promise()._runner = &_pool;
        if (pool_node_ptr new_node; _pool._mempool.capture(new_node)) {
            new_node->_data = std::move(new_task);
            reattach_front(new_node, this);
        }
    }


    inline double runner::velocity() const noexcept {
        if (_quants.value() == 0) [[unlikely]] return 0.0;
        return static_cast<double>(_tasks_amount) / static_cast<double>(_quants.value());
    }


    inline bool runner::yank() noexcept {
        using namespace nukes::details::nodes;

        promise_lifecycle touch_result = e_executed;
        omni_node task_unit = fetch_task_node();

        // NOTE: If can not pull task
        if (not task_unit) [[unlikely]]
            return yank_vortex();

        // NOTE: Prefetching next task frame
        if (const auto head = _pool.inspect_head()) [[likely]]
            head->_data.prefetch();

        // NOTE: Proceeding async
        task_unit->_data.awake(&touch_result);

        // NOTE: Checking if async can be resumed
        const bool is_resumable {
            task_unit->_data
            and touch_result not_eq e_failed
            and touch_result not_eq e_finished
            and touch_result not_eq e_detached
        };

        // NOTE: If task is idle, releasing its node.
        if (not is_resumable) {
            _insert_pool.release_node(task_unit);
            --_tasks_amount;
            return true;
        }

        // NOTE: Forwarding the async via passed router if needed
        if (task_unit->_data._coroutine.promise()._runner_router) [[likely]] {
            task_unit->_data._coroutine.promise()._runner_router->redirect(task_unit);
            return true;
        }

        // NOTE: If task is a service unit then placing it to the vortex pool
        if (task_unit->_data._coroutine.promise()._polling) {
            _vortex_pool.push_node(task_unit);
            return true;
        }

        // NOTE: Returning task back to the local pool on this step
        _pool.push_node(task_unit);
        return true;
    }


    inline bool runner::yank_vortex() noexcept {

        promise_lifecycle touch_result = e_executed;
        omni_node vortex_unit = _vortex_pool.pop_node();

        // NOTE: If node is empty breaking
        if (not vortex_unit) [[unlikely]] return false;

        // NOTE: Proceeding async
        vortex_unit->_data.awake(&touch_result);

        // NOTE: Checking if async can be resumed
        const bool is_resumable {
            vortex_unit->_data
            and touch_result not_eq e_failed
            and touch_result not_eq e_finished
            and touch_result not_eq e_detached
        };

        // NOTE: If task is idle, releasing its node.
        if (not is_resumable) {
            _insert_pool.release_node(vortex_unit);
            --_tasks_amount;
            return true;
        }

        // NOTE: Forwarding the async via passed router if needed
        if (vortex_unit->_data._coroutine.promise()._runner_router) [[likely]] {
            vortex_unit->_data._coroutine.promise()._runner_router->redirect(vortex_unit);
            return true;
        }

        // NOTE: Returning task back to the local pool on this step
        _vortex_pool.push_node(vortex_unit);
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
                // NOTE: Trying to switch to interthread pool
                if (_pull_source not_eq pull_source::e_interthread_pool and not _insert_pool.empty())
                    _pull_source = pull_source::e_interthread_pool;
            }
        }
        current_runner_ptr = nullptr;
        return i not_eq 0 or yank_vortex();
    }

    inline omni_node runner::fetch_task_node() {
        using namespace nukes::details::nodes;
        omni_node task_unit {};
        // NOTE: Trying to fetch from local pool
        if (_pull_source == pull_source::e_local_pool) {
            task_unit = _pool.pop_node();
            // NOTE: In case when cannot fetch then switching to interthread pool
            if (not task_unit)
                _pull_source = pull_source::e_interthread_pool;
        }
        // NOTE: Trying to fetch from interthread pool
        if (_pull_source == pull_source::e_interthread_pool) {
            task_unit = _insert_pool.pop_node();
            // NOTE: In case when cannot fetch then switching to local pool
            if (not task_unit)
                _pull_source = pull_source::e_local_pool;
        }
        return task_unit;
    }
} // end namespace ace::core


inline thread_local ace::omni_runner ace::core::runner::current_runner_ptr {};

template<typename returnT, ace::core::is_promise_rule promise_rule_t>
inline auto ace::core::async<returnT, promise_rule_t>::get_current_pool() noexcept
-> runner_pool_t* { return runner::get(); }

//==============================DEFINITIONS==================================

#undef ACE_RUNNER_META
#undef ACE_RUNNER_MEMBER
#endif // ACE_RUNNER_H
