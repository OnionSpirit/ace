/**
 * @file runner.h
 * @brief Per-thread task runner — the core execution unit of the ACE runtime.
 *
 * @details Each @c runner owns a single-thread local task queue (@c _pool), an
 * MPSC inter-thread insertion queue (@c _insert_pool), and a service
 * pool (@c _service_pool).  The dispatcher assigns tasks to runners via
 * @c attach() / @c attach_front().
 *
 * ### Execution loop
 *
 * @c run() processes up to 128 tasks per call (the @c yank_limit). Every
 * 16 completed pulls it gives a non-empty alternate task source a turn and
 * processes service tasks, bounding starvation without switching after the
 * first item of a newly selected FIFO source.
 *
 * ### Task lifecycle inside a runner
 *
 * 1. @c attach()/@c attach_front() — sets @c _runner on the promise
 *    and enqueues the task.
 * 2. @c yank() — pops a task, calls @c awake(), and decides whether to
 *    re-queue, release, or redirect via router.
 * 3. @c reattach() — returns a task to its owning runner (used by futures
 *    to wake a suspended coroutine).
 *
 * @see ace::core::dispatcher, ace::core::async
 */
#ifndef ACE_RUNNER_H
#define ACE_RUNNER_H

#include <atomic>
#include <nukes/dynamic/mpsc_queue.h>

#include "ace/core/tools/macro.h"
#include "ace/core/async.h"


namespace ace::core {

    /**
     * @brief Per-thread coroutine execution manager.
     *
     * @details Owns three task queues:
     *  - @c _pool — single-thread queue for local tasks (fast path).
     *  - @c _insert_pool — MPSC queue for cross-thread inserts. Nukes owns the
     *    queue-local reclamation protocol, so producers need no runner-wide gate.
     *  - @c _service_pool — queue for low-priority polling tasks (service routines).
     *
     * Publishes the number of runnable or currently executing nodes through
     * @c load(). Suspended nodes owned by an external router are not counted.
     */
    struct runner {

        /// @brief Queue type for cross-thread task insertion.
        typedef nukes::dynamic::mpsc_queue<task> insert_pool_t;

        /// @brief Node type of the local task pool.
        typedef runner_pool_t::node_t *pool_node_ptr;
        /// @brief Node type of the cross-thread insertion pool.
        typedef insert_pool_t::node_t *insert_node_ptr;

        /**
         * @brief Source pool selection for the next @c fetch_task_node().
         */
        enum class pull_source : uint8_t {
            e_local_pool,        ///< Pull from the local pool.
            e_interthread_pool,  ///< Pull from the inter-thread insertion pool.
        };

        ACE_CACHE_LINE(0)

        mutable runner_pool_t           _pool               {}; ///< Pool of the assigned tasks
        runner_pool_t                   _service_pool       {}; ///< Pool of low-priority polling tasks (service routines).

        ACE_CACHE_LINE(1)

        mutable insert_pool_t           _insert_pool        {}; ///< Pool for the interthread insertion

        ACE_CACHE_LINE(6)

        std::atomic<std::size_t>         _runnable_load  {};                             ///< Runnable and currently executing nodes.
        std::atomic<std::uint64_t>       _wake_epoch     {};                             ///< Changes whenever work is published to this runner.
        std::atomic<std::uint64_t>*      _activity_epoch {};                             ///< Dispatcher transition notification; null for standalone runners.
        pull_source                      _pull_source    { pull_source::e_local_pool };  ///< Pool selected for the next fetch.

        static thread_local omni_runner current_runner_ptr; ///< Runner active on the current thread (set inside @c run()).

        /// @brief Default constructor.
        runner() = default;

        // TODO: Need to figure out how to validate this wo warn cuz its important
        // {
        //     static_assert(offsetof(runner, _pool) == 0,
        //         "'_pool' must be the first member of runner. Stop touching not your code idiot");
        // };

        /// @brief Default destructor.
        ~runner() = default;

        /**
         * @brief Move constructor — transfers all pools and task count.
         * @param t Source runner to move from.
         */
        runner(runner &&t) noexcept;

        /**
         * @brief Move assignment — transfers all pools and task count.
         * @param t Source runner to move from.
         * @return Reference to this runner.
         */
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
         * @brief Returns the published scheduling load.
         * @details Counts nodes that are queued or currently executing. Nodes
         * suspended in an external router are excluded until reattachment.
         * @return Current runnable load.
         */
        [[nodiscard]] std::size_t load() const noexcept {
            return _runnable_load.load(std::memory_order_relaxed);
        }

        /**
         * @brief Checks quiescence with acquire semantics.
         * @details Observing zero synchronizes with the final runnable release,
         * making task writes visible before @c run() returns or reload proceeds.
         */
        [[nodiscard]] bool quiescent() const noexcept {
            return _runnable_load.load(std::memory_order_acquire) == 0;
        }

        /**
         * @brief Connects this runner to the dispatcher's activity epoch.
         * @param activity_epoch Epoch notified on idle-to-active and active-to-idle transitions.
         * @warning The pointed-to atomic must outlive this runner.
         */
        void bind_activity_epoch(std::atomic<std::uint64_t>* activity_epoch) noexcept {
            _activity_epoch = activity_epoch;
        }

        /// @brief Returns the current per-runner wake sequence.
        [[nodiscard]] std::uint64_t wake_epoch() const noexcept {
            return _wake_epoch.load(std::memory_order_acquire);
        }

        /**
         * @brief Blocks until this runner's wake sequence changes.
         * @param observed Sequence value previously returned by @c wake_epoch().
         */
        void wait_for_work(const std::uint64_t observed) const noexcept {
            _wake_epoch.wait(observed, std::memory_order_acquire);
        }

        /// @brief Wakes a worker waiting for this runner.
        void notify_worker() noexcept {
            _wake_epoch.fetch_add(1, std::memory_order_release);
            _wake_epoch.notify_one();
        }

        /**
         * @details Resumes only one ready task
         * @return @b true if task was processed, @b false otherwise
         */
        bool yank() noexcept;

        /**
         * @details Resumes only service polling tasks
         * @return @b true if task was processed, @b false otherwise
         */
        bool yank_service() noexcept;

        /**
         * @details Checks if runner has only service polling tasks
         * @return @c true if runner has only service tasks, @c false otherwise
         */
        bool is_polling() const noexcept {
            return _pool.empty() and not _service_pool.empty();
        };

        /**
         * @details Resumes tasks from the ready task pool until it is empty, or limit (1024) reached.
         * @return @b true if runner made some tasks, @b false otherwise
         */
        bool run() noexcept;

        // TODO: Add support for automaton in the future
        /**
         * @details Function to attach task to the runner
         * @param new_task Task to be pushed into the runner
         */
        template <typename async_return_t, template <typename> typename promise_rule_t>
        requires is_spawnable_rule<promise_rule_t>
        void attach(async<async_return_t, promise_rule_t> &&new_task);

        // TODO: Add support for automaton in the future
        /**
         * @details Function to attach task to the runner
         * @param new_task Task to be pushed into the runner
         */
        template <typename async_return_t, template <typename> typename promise_rule_t>
        requires is_spawnable_rule<promise_rule_t>
        void attach_front(async<async_return_t, promise_rule_t> &&new_task);

        /**
         * @details Checks if any Tasks stored in the runner
         * @return @b true if empty, @b false otherwise
         */
        [[nodiscard]] bool empty() const noexcept {
            return _pool.empty() and _service_pool.empty() and _insert_pool.empty();
        };

        /**
         * @brief Fetches one task node from the selected pool.
         * @details Alternates between the local pool and the insertion pool
         * when the current one is empty.
         * @return The fetched node, or a null node when both pools are empty.
         */
        omni_node fetch_task_node();

    private:

        /// @brief Publishes one newly runnable node before queue insertion.
        void acquire_runnable() noexcept;

        /// @brief Removes one node after it leaves the runnable state.
        void release_runnable() noexcept;

        /// @brief Captures one insertion node without racing the SPMC freelist consumer path.
        [[nodiscard]] insert_node_ptr capture_insert_node() noexcept;

        /// @brief Pops one insertion node while excluding concurrent freelist capture/release.
        [[nodiscard]] insert_node_ptr pop_insert_node() noexcept;

        /// @brief Publishes one node while excluding other insertion-queue transitions.
        [[nodiscard]] bool push_insert_node(insert_node_ptr node) noexcept;

        /// @brief Returns one completed node to the insertion freelist safely.
        void release_insert_node(insert_node_ptr node) noexcept;

        /**
         * @brief Carrier wrapper for valued tasks: resumes the inner coroutine
         * until it finishes or is canceled.
         * @param inner Valued task to carry.
         */
        template <typename async_return_t>
        static task carrier(async<async_return_t> inner) {
            while (not inner._coroutine.done() and inner._coroutine.promise().status() not_eq e_canceled)
                co_await carrier_suspend{inner};
            co_return;
        }

        /**
         * @brief Carrier wrapper for automaton tasks.
         * @param inner Automaton task to carry.
         */
        template <typename async_return_t>
        static task carrier(async<async_return_t, automaton_rule> inner) {
            while (not inner._coroutine.done() and inner._coroutine.promise().status() not_eq e_canceled)
                co_await automaton_suspend{inner};
            co_return;
        }

        /**
         * @brief Awaitable used by @c carrier for ordinary valued tasks.
         * @details Steals the inner coroutine's router and roaming state
         * onto the carrier promise before suspending.
         * @tparam async_return_t Return type of the carried task.
         */
        template <typename async_return_t>
        struct carrier_suspend final : traits::future_traits<carrier_suspend<async_return_t>> {
            async<async_return_t>& _inner; ///< Carried task.
            IMPORT_FUTURE_ENV(carrier_suspend)

            /**
             * @brief Constructs the suspend point.
             * @param inner Carried task.
             */
            explicit carrier_suspend(async<async_return_t>& inner) : _inner(inner) {}

            /**
             * @brief @c true when the inner task is done, canceled or immediately resumable.
             */
            bool await_ready() override {
                if (_inner._coroutine.done()) return true;
                if (_inner._coroutine.promise().status() == e_canceled) return true;
                return _inner.await_ready();
            }

            /**
             * @brief Propagates roaming, status and router from the inner task.
             * @param coroutine Carrier's promise accessor.
             * @return Always @c true — the carrier always suspends.
             */
            bool await_suspend(auto coroutine) { return _inner.await_suspend(coroutine); }

            /// @brief No value produced.
            void await_resume() { }
        };

        /**
         * @brief Awaitable used by @c carrier for automaton tasks.
         * @details Mirrors @c carrier_suspend but also re-attaches the
         * yield waiter when the automaton finishes or yields.
         * @tparam async_return_t Return type of the carried automaton.
         */
        template <typename async_return_t>
        struct automaton_suspend final : traits::future_traits<automaton_suspend<async_return_t>> {
            async<async_return_t, automaton_rule>& _inner; ///< Carried automaton.
            IMPORT_FUTURE_ENV(automaton_suspend)

            /**
             * @brief Constructs the suspend point.
             * @param inner Carried automaton.
             */
            explicit automaton_suspend(async<async_return_t, automaton_rule>& inner) : _inner(inner) {}

            bool await_ready() override {
                if (_inner._coroutine.done()) {
                    try_reattach_waiter();
                    return true;
                }
                if (_inner._coroutine.promise().status() == e_canceled) {
                    try_reattach_waiter();
                    return true;
                }
                if (_inner._coroutine.promise().status() == e_executed_with_value) {
                    try_reattach_waiter();
                    return false;
                }
                const bool inner_ready = _inner.await_ready();
                if (_inner._coroutine.done()) {
                    try_reattach_waiter();
                    return true;
                }
                if (_inner._coroutine.promise().status() == e_executed_with_value)
                    try_reattach_waiter();
                return inner_ready;
            }

        private:
            /**
             * @brief Re-attaches the pending yield waiter, if any.
             */
            void try_reattach_waiter() {
                if (_inner._coroutine.promise()._yield_waiter) {
                    runner::reattach(_inner._coroutine.promise()._yield_waiter);
                    _inner._coroutine.promise()._yield_waiter.reset();
                }
            }
        public:

            /**
             * @brief Propagates roaming, status and router from the automaton.
             * @param coroutine Carrier's promise accessor.
             * @return Always @c true — the carrier always suspends.
             */
            bool await_suspend(auto coroutine) { return _inner.await_suspend(coroutine); }

            /// @brief No value produced.
            void await_resume() { }
        };

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
        this->_service_pool = std::move(t._service_pool);
        this->_insert_pool = std::move(t._insert_pool);
        this->_pull_source = t._pull_source;
        this->_runnable_load.store(
            t._runnable_load.exchange(0, std::memory_order_relaxed),
            std::memory_order_relaxed);
        this->_wake_epoch.store(
            t._wake_epoch.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        this->_activity_epoch = t._activity_epoch;
        t._activity_epoch = nullptr;
    };

    inline runner& runner::operator=(runner &&t) noexcept {
        this->_pool = std::move(t._pool);
        this->_service_pool = std::move(t._service_pool);
        this->_insert_pool = std::move(t._insert_pool);
        this->_pull_source = t._pull_source;
        this->_runnable_load.store(
            t._runnable_load.exchange(0, std::memory_order_relaxed),
            std::memory_order_relaxed);
        this->_wake_epoch.store(
            t._wake_epoch.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        this->_activity_epoch = t._activity_epoch;
        t._activity_epoch = nullptr;
        return *this;
    };


    inline void runner::acquire_runnable() noexcept {
        const std::size_t previous = _runnable_load.fetch_add(1, std::memory_order_seq_cst);
        if (previous == 0) {
            notify_worker();
            if (_activity_epoch) {
                _activity_epoch->fetch_add(1, std::memory_order_seq_cst);
                _activity_epoch->notify_all();
            }
        }
    }


    inline void runner::release_runnable() noexcept {
        const std::size_t previous = _runnable_load.fetch_sub(1, std::memory_order_seq_cst);
        if (previous == 1 and _activity_epoch) {
            _activity_epoch->fetch_add(1, std::memory_order_seq_cst);
            _activity_epoch->notify_all();
        }
    }


    inline runner::insert_node_ptr runner::capture_insert_node() noexcept {
        insert_node_ptr node = nullptr;
        const bool captured = _insert_pool._mempool.capture(node);
        return captured ? node : nullptr;
    }


    inline runner::insert_node_ptr runner::pop_insert_node() noexcept {
        return _insert_pool.pop_node();
    }


    inline bool runner::push_insert_node(insert_node_ptr node) noexcept {
        return _insert_pool.push_node(node);
    }


    inline void runner::release_insert_node(insert_node_ptr node) noexcept {
        _insert_pool.release_node(node);
    }


    inline void runner::reattach_impl(omni_node& node, const omni_runner local_runner_ptr) {
        using namespace nukes::detail::nodes;
        if (not node or not node->_data.is_exist()) [[unlikely]]
            throw std::runtime_error { "trying to 'reattach' idle context" };
        omni_runner target_runner_ptr = node->_data._coroutine.promise()._runner;
        if (not target_runner_ptr) [[unlikely]]
            throw std::logic_error {
                "'reattach' operation can't be applied to 'ace::core::async<...>'s "
                "which are not running at the 'ace::core::runner'"
            };
        target_runner_ptr.as<runner>()->acquire_runnable();
        node->_data.release_router();
        if (local_runner_ptr == target_runner_ptr)
            local_runner_ptr->_pool.push_node(node);
        else if (not target_runner_ptr->push_insert_node(node)) {
            target_runner_ptr.as<runner>()->release_runnable();
            throw std::runtime_error { "failed to publish a reattached task" };
        }
    }


    inline void runner::reattach_front_impl(omni_node& node, const omni_runner local_runner_ptr) {
        using namespace nukes::detail::nodes;
        if (not node or not node->_data.is_exist()) [[unlikely]]
            throw std::runtime_error { "trying to 'reattach_front' idle context" };
        omni_runner target_runner_ptr = node->_data._coroutine.promise()._runner;
        if (not target_runner_ptr) [[unlikely]]
            throw std::logic_error {
                "'reattach_front' operation can't be applied to 'ace::core::async<...>'s "
                "which are not running at the 'ace::core::runner'"
            };
        target_runner_ptr.as<runner>()->acquire_runnable();
        node->_data.release_router();
        if (local_runner_ptr == target_runner_ptr) {
            node->_data.prefetch();
            local_runner_ptr->_pool.push_node_front(node);
            local_runner_ptr->_pull_source = pull_source::e_local_pool;
        } else if (not target_runner_ptr->push_insert_node(node)) {
            target_runner_ptr.as<runner>()->release_runnable();
            throw std::runtime_error { "failed to publish a reattached task" };
        }
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


    template<typename returnT, template <typename> typename promise_rule_t>
    requires is_rule<promise_rule_t>
    void async<returnT, promise_rule_t>::release_waiters() {
        if constexpr (is_spawnable_rule<promise_rule_t>) {
            if (_coroutine.promise()._waiters) {
                omni_node waiter = _coroutine.promise()._waiters->pop_node();
                while (waiter.operator bool() and waiter->_data.is_exist()) {
                    waiter->_data.release_future();
                    runner::reattach(waiter);
                    waiter = _coroutine.promise()._waiters->pop_node();
                }
            }
        }
    }


    template <typename async_return_t, template <typename> typename promise_rule_t>
        requires is_spawnable_rule<promise_rule_t>
    void runner::attach(async<async_return_t, promise_rule_t> &&new_task) {
        new_task._coroutine.promise()._runner = &_pool;
        if (insert_node_ptr new_node = capture_insert_node()) {
            if constexpr (std::is_void_v<async_return_t>)
                new_node->_data = std::move(new_task);
            else
                new_node->_data = carrier(std::move(new_task));
            reattach(new_node, current_runner_ptr);
            return;
        }
        throw std::bad_alloc {};
    }


    template <typename async_return_t, template <typename> typename promise_rule_t>
        requires is_spawnable_rule<promise_rule_t>
    void runner::attach_front(async<async_return_t, promise_rule_t> &&new_task) {
        new_task._coroutine.promise()._runner = &_pool;
        if (insert_node_ptr new_node = capture_insert_node()) {
            if constexpr (std::is_void_v<async_return_t>)
                new_node->_data = std::move(new_task);
            else
                new_node->_data = carrier(std::move(new_task));
            reattach_front(new_node, current_runner_ptr);
            return;
        }
        throw std::bad_alloc {};
    }


    inline bool runner::yank() noexcept {
        using namespace nukes::detail::nodes;

        promise_lifecycle touch_result = e_executed;
        omni_node task_unit = fetch_task_node();

        // NOTE: If can not pull task
        if (not task_unit) [[unlikely]]
            return yank_service();

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
            and touch_result not_eq e_canceled
        };

        // NOTE: If task is idle, releasing its node.
        if (not is_resumable) {
            release_insert_node(task_unit);
            release_runnable();
            return true;
        }

        // NOTE: Forwarding the async via passed router if needed
        if (task_unit->_data._coroutine.promise()._runner_router) [[likely]] {
            const bool ownership_transferred =
                task_unit->_data._coroutine.promise()._runner_router->redirect(task_unit);
            release_runnable();
            if (not ownership_transferred) {
                task_unit->_data.release_router();
                reattach(task_unit, current_runner_ptr);
            }
            return true;
        }

        // NOTE: If task is a service unit then placing it to the service pool
        if (task_unit->_data._coroutine.promise()._polling) {
            _service_pool.push_node(task_unit);
            return true;
        }

        // NOTE: Returning task back to the local pool on this step
        _pool.push_node(task_unit);
        return true;
    }


    inline bool runner::yank_service() noexcept {

        promise_lifecycle touch_result = e_executed;
        omni_node service_unit = _service_pool.pop_node();

        // NOTE: If node is empty breaking
        if (not service_unit) [[unlikely]] return false;

        // NOTE: Proceeding async
        service_unit->_data.awake(&touch_result);

        // NOTE: Checking if async can be resumed
        const bool is_resumable {
            service_unit->_data
            and touch_result not_eq e_failed
            and touch_result not_eq e_finished
            and touch_result not_eq e_canceled
        };

        // NOTE: If task is idle, releasing its node.
        if (not is_resumable) {
            release_insert_node(service_unit);
            release_runnable();
            return true;
        }

        // NOTE: Forwarding the async via passed router if needed
        if (service_unit->_data._coroutine.promise()._runner_router) [[likely]] {
            const bool ownership_transferred =
                service_unit->_data._coroutine.promise()._runner_router->redirect(service_unit);
            release_runnable();
            if (not ownership_transferred) {
                service_unit->_data.release_router();
                reattach(service_unit, current_runner_ptr);
            }
            return true;
        }

        // NOTE: Returning task back to the local pool on this step
        _service_pool.push_node(service_unit);
        return true;
    }


    inline bool runner::run() noexcept {
        int i = 0;
        current_runner_ptr = this;
        for (constexpr int yank_limit = 128; i < yank_limit and yank(); ++i) {
            if (i % 16 == 15) {
                yank_service();
                // Bound starvation without abandoning the source after its
                // first item. This lets an externally published FIFO batch
                // reach the local queue before a just-suspended task reruns.
                if (_pull_source == pull_source::e_local_pool and not _insert_pool.empty())
                    _pull_source = pull_source::e_interthread_pool;
                else if (_pull_source == pull_source::e_interthread_pool and not _pool.empty())
                    _pull_source = pull_source::e_local_pool;
            }
        }
        current_runner_ptr = nullptr;
        return i not_eq 0 or yank_service();
    }

    inline omni_node runner::fetch_task_node() {
        using namespace nukes::detail::nodes;
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
            task_unit = pop_insert_node();
            // NOTE: In case when cannot fetch then switching to local pool
            if (not task_unit)
                _pull_source = pull_source::e_local_pool;
        }
        return task_unit;
    }
} // end namespace ace::core


inline thread_local ace::omni_runner ace::core::runner::current_runner_ptr {};

template<typename returnT, template <typename> typename promise_rule_t>
requires ace::core::is_rule<promise_rule_t>
auto ace::core::async<returnT, promise_rule_t>::get_current_pool() noexcept
-> runner_pool_t* { return runner::get(); }

//==============================DEFINITIONS==================================

#undef ACE_RUNNER_META
#undef ACE_RUNNER_MEMBER
#endif // ACE_RUNNER_H
