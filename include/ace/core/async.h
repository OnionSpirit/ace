/**
 * @file async.h
 * @brief Core coroutine async type (@c ace::coroutines::async<T, Rule>)
 *        and the public @c ace::async<T> / @c ace::promise<T> aliases.
 *
 * @details This file defines the central type of the ACE framework.
 * @c async<returnT, promise_rule_t> is a C++20 coroutine type that:
 *
 *  - Inherits from @c busy_future_traits<async> so that it can be directly
 *    @c co_await-ed from another coroutine (nested coroutines).
 *  - Owns its @c std::coroutine_handle and destroys it on destruction.
 *  - Carries a @c runner_router_slot_t that futures use to redirect the
 *    async into their own waiting structures (router pattern).
 *  - Can expose a @c control_block_handle via @c observe() for external
 *    join / cancel operations.
 *
 * ### Public aliases
 *
 * @code{.cpp}
 * // Lazy coroutine — suspends at creation (initial_suspend = suspend_always)
 * template<typename T = void> using ace::async   = async<T, differed>;
 *
 * // Eager coroutine — runs immediately (initial_suspend = suspend_never)
 * template<typename T = void> using ace::promise = async<T, permanent>;
 * @endcode
 *
 * @see ace::async, ace::promise, ace::coroutines::promise_traits,
 *      ace::coroutines::control_block
 */
#ifndef ACE_ASYNC_H
#define ACE_ASYNC_H

#include <coroutine>
#include <cstring>
#include <expected>
#include <iostream>

#include <nukes/dynamic/regular_queue.h>
#include <nukes/details/prefetch.h>

#include "ace/core/traits/future.h"
#include "ace/core/traits/promise.h"
#include "ace/core/tools/macro.h"
#include "ace/core/control.h"
#include "ace/core/traits/routing.h"
#include "nukes/dynamic/mpsc_queue.h"
#include "tools/omniptr.h"


// TODO: Move yield operation to generator,
// make generator overload iterators
// so it can be used in a for loop
namespace ace::core {

    struct runner;

    /**
     * @brief Core coroutine async type.
     *
     * @details @c async<returnT, promise_rule_t> represents a single
     * coroutine instance.  It is move-only (copy is deleted) and owns the
     * underlying @c std::coroutine_handle.
     *
     * The type itself satisfies the @c busy_future_traits concept so that one
     * async can be directly @c co_await-ed inside another, enabling nested
     * coroutines.
     *
     * @tparam returnT        Value type returned by @c co_return.
     *                        Defaults to @c void.
     * @tparam promise_rule_t Return and initial suspension policy type.
     *
     * @see ace::async, ace::promise
     */
    template<typename returnT =void, template <typename> typename promise_rule_t = lazy_rule>
    requires is_rule<promise_rule_t>
    struct ACE_AWAIT_NODISCARD async : traits::busy_future_traits<async<returnT, promise_rule_t>> {

        IMPORT_BUSY_FUTURE_ENV(async)

        struct promise_type;

        /// @brief Implemented rule type
        typedef promise_rule_t<returnT> rule_t;

        /// @brief Type of the underlying coroutine handle.
        typedef std::coroutine_handle<promise_type>                            coroutine_t;
        /// @brief Queue type used as the runner's task pool.
        typedef nukes::dynamic::reg_queue<async<>>                             runner_pool_t;
        /// @brief Queue type used as the runner's task pool.
        typedef nukes::dynamic::mpsc_queue<async<>>                            insert_pool_t;
        /// @brief Secured void* for any nodes
        typedef tools::omniptr<runner_pool_t::node_t, insert_pool_t::node_t>   omni_node;
        /// @brief Secured void* for runner and it's pool
        typedef tools::omniptr<runner, runner_pool_t>                          omni_runner;
        /// @brief Abstract router interface for this async type.
        typedef traits::runner_router_handle<omni_node>                        runner_router;
        /// @brief In-place storage slot for a router object.
        typedef traits::router_slot<runner_router>                             runner_router_slot_t;

        coroutine_t _coroutine; ///< Underlying coroutine handle.  Null after move.

        bool*                  _outer_roaming { nullptr };
        runner_router_slot_t*  _outer_router  { nullptr };
        control_block*         _outer_block   { nullptr };

        /// @brief Helper to get active runner pool ptr or @c nullptr
        /// if @c async<...> is constructed out of runner context
        static runner_pool_t* get_current_pool() noexcept;

        async() = default;

        /**
         * @brief Move constructor.  Transfers ownership of the coroutine handle.
         * @param ctx  Source async.  Its @c _coroutine is set to null.
         */
        async(async && ctx) noexcept {
            _coroutine = std::forward<coroutine_t>(ctx._coroutine);
            ctx._coroutine = nullptr;
        };

        /**
         * @brief Move assignment.  Transfers ownership of the coroutine handle.
         * @param ctx  Source async.  Its @c _coroutine is set to null.
         * @return Reference to @c *this.
         */
        async &operator=(async && ctx)  noexcept {
            _coroutine = std::forward<coroutine_t>(ctx._coroutine);
            ctx._coroutine = nullptr;
            return *this;
        };

        async(const async &) = delete;             ///< Contexts are move-only.
        async &operator=(const async &) = delete;  ///< Contexts are move-only.

        /**
         * @brief Construct from a raw coroutine handle.
         * @details Used internally by @c promise_type::get_return_object().
         * @param handler  Coroutine handle to take ownership of.
         */
        explicit async(coroutine_t &&handler) : _coroutine{handler} {
            _coroutine.promise().setup_control_block(_coroutine);
        };

        /**
         * @brief Check whether the coroutine is exist.
         * @details Returns @c true iff all of the following hold:
         *  - The handle is non-null.
         *  - The coroutine has not finished (@c !done()).
         *  - The control block has not been disowned (not cancelled).
         * @return @c true if resumable.
         */
        [[nodiscard]] bool is_exist() const noexcept {
            return _coroutine and not _coroutine.done();
        }

        /// @brief Equivalent to @c is_exist().
        explicit operator bool() const { return is_exist(); }

        /// @brief Propagates status parameters to outer coroutine
        void propagate() {
            if (not _outer_roaming or not _outer_block or not _outer_router)
                return;
            // NOTE: Storing local value of roaming into outer
            *_outer_roaming = _coroutine.promise()._roaming;
            // NOTE: Storing local value of status into outer
            _outer_block->_status = _coroutine.promise().status();
            // NOTE: Just coping router ptr. Outer task will destroy router before current promise stack
            *_outer_router << _coroutine.promise()._runner_router;
        }

        /// @brief Setting outer status params refs
        void setup_outer(auto& outer) {
            if (_outer_roaming or _outer_block or _outer_router)
                return;
            _outer_roaming = &outer.promise()._roaming;
            _outer_block = outer.promise()._block;
            _outer_router = &outer.promise()._runner_router;
        }

        /**
         * @brief Destructor. Wakes all registered waiters
         * @details Decrements the strong reference count of the control
         * block (if any) before suspending.
         * The coroutine frame is not always
         * destroyed here; the runner or owning @c async destructor does
         * that.
         */
        ~async() override {
            if (_coroutine) {
                release_waiters();
                // NOTE: Canceling task if it is destructed incomplete
                if (_coroutine.promise()._runner_router) {
                    _coroutine.promise()._runner_router->cancel();
                    _coroutine.promise()._runner_router.release();
                }
                // NOTE: Destroying stack only if it is become untracked
                if (control_block::untrack(_coroutine.promise()._block))
                    _coroutine.destroy();
            }
        };

        /**
         * @brief Release the currently-held future and clear the busy-future pointer.
         * @details Called by the runner before resuming a async that was
         * previously forwarded by a router.
         */
        void release_future() {
            _coroutine.promise()._runner_router.release();
            _coroutine.promise()._busy_future = nullptr;
        }

        /**
         * @brief Release the currently-held future router.
         * @details Called by the runner at reattach operation
         */
        void release_router() {
            _coroutine.promise()._runner_router.release();
        }

        /**
         * @brief Check whether the async is ready to be resumed by the runner.
         * @details Returns @c true if no busy future is pending, or if the
         * pending busy future has become ready (@c await_ready() returns @c true).
         * @return @c true if the runner may resume this async.
         */
        bool is_resumable() {
            return (not _coroutine.promise()._busy_future or _coroutine.promise()._busy_future->await_ready())
                and not _coroutine.promise()._runner_router;
        }

        /**
         * @brief Create a @c control_block_handle that allows external
         *        join / cancel operations on this coroutine.
         * @details Lazily initializes the control block and the internal
         * @c async_router.  Safe to call multiple times; subsequent calls
         * return handles that share the same underlying block.
         * @return A new @c control_block_handle with an incremented weak ref-count.
         * @note The async must not have been moved away before calling @c observe().
         */
        control_block_handle observe() {
            // NOTE: Setting up promise block by coroutine
            _coroutine.promise().setup_control_block(_coroutine);
            return control_block_handle{ _coroutine };
        }

        /**
         * @brief Wake all coroutines that are waiting for this async to finish.
         * @details Drains the @c _waiters queue and re-attaches each async to
         * its own runner pool.  Called automatically from the destructor.
         */
        void release_waiters() {
            if constexpr (is_spawnable_rule<promise_rule_t>) {
                if (_coroutine.promise()._waiters) {
                    omni_node waiter = _coroutine.promise()._waiters->pop_node();
                    while (waiter.operator bool() and waiter->_data.is_exist()) {
                        waiter->_data.release_future();
                        waiter->_data._coroutine.promise()._runner.as<runner_pool_t>()->push_node(waiter);
                        waiter = _coroutine.promise()._waiters->pop_node();
                    }
                }
            }
        }

        /**
         * @brief Allocate a debug trace ID for this async.
         * @details The trace ID is unique for the lifetime of the async and
         * can be used to correlate log entries across asynchronous boundaries.
         * @return The allocated trace ID on success, or an error string if the
         *         coroutine handle is null.
         */
        std::expected<std::size_t, std::string_view> track() {
            if (_coroutine)
                return _coroutine.promise().setup_trace();
            return std::unexpected("async is already dead.");
        }

        void prefetch() const {
            const control_block* frame = control_block::get_block_from_address(_coroutine.address());
            const std::size_t frame_size = frame->_frame_size;
            for (std::size_t i = 0; i <= frame_size / ACE_CACHE_LINE_SIZE; ++i) {
                const void* cacheline_ptr = frame + (2 * i);
                nukes::details::prefetch<nukes::details::e_temporal>(cacheline_ptr);
            }
        }

        class async_router : public traits::async_router_handle {

            void* _address { nullptr };

        public:

            async_router() = default;

            explicit async_router(const coroutine_t& coroutine)
                : _address(coroutine.address()) {}

            void cancel() noexcept override {
                if (not _address) [[unlikely]] return;
                auto handle = coroutine_t::from_address(_address);
                if (handle and handle.promise()._runner_router) [[likely]] {
                    handle.promise()._runner_router->cancel();
                    handle.promise()._runner_router.release();
                }
                handle.promise().status(e_canceled);
            }

            bool redirect(void* undefined_waiter) noexcept override {
                if constexpr (is_spawnable_rule<promise_rule_t>) {
                    if (not _address or not undefined_waiter) [[unlikely]] return false;
                    auto handle = coroutine_t::from_address(_address);
                    auto waiter = omni_node(undefined_waiter);
                    handle.promise()._waiters = std::make_shared<runner_pool_t>();
                    handle.promise()._waiters->push_node(std::forward<omni_node>(waiter));
                }
                return true;
            }

            bool return_value(void* mem_ptr) noexcept override {
                if constexpr (requires(promise_type promise_t) { promise_t._return_value; }) {
                    if (not _address) [[unlikely]] return false;
                    auto handle = coroutine_t::from_address(_address);
                    auto ref = static_cast<returnT*>(mem_ptr);
                    *ref = std::forward<returnT>(handle.promise()._return_value);
                }
                return true;
            }

            bool yield_value(void* mem_ptr) noexcept override {
                if constexpr (is_automaton_rule<promise_rule_t>) {
                    if (not _address) [[unlikely]] return false;
                    auto handle = coroutine_t::from_address(_address);
                    if (handle.promise().status() not_eq e_executed_with_value) return false;
                    auto ref = static_cast<returnT*>(mem_ptr);
                    *ref = std::forward<returnT>(handle.promise()._return_value);
                    handle.promise().status(e_executed);
                }
                return true;
            }

            bool has_yield() noexcept override {
                if constexpr (is_automaton_rule<promise_rule_t>) {
                    if (not _address) return false;
                    auto handle = coroutine_t::from_address(_address);
                    return handle.promise().status() == e_executed_with_value and not handle.done();
                }
                return false;
            }

            bool set_yield_waiter(void* node_ptr) noexcept override {
                if constexpr (is_automaton_rule<promise_rule_t>) {
                    if (not _address or not node_ptr) return false;
                    auto handle = coroutine_t::from_address(_address);
                    handle.promise()._yield_waiter = omni_node(node_ptr);
                }
                return true;
            }

            bool cancel_yield() noexcept override {
                if constexpr (is_automaton_rule<promise_rule_t>) {
                    if (not _address) return false;
                    auto handle = coroutine_t::from_address(_address);
                    handle.promise()._yield_waiter.reset();
                }
                return true;
            }

            void destroy() noexcept override {
                if (not _address) [[unlikely]] return;
                auto handle = coroutine_t::from_address(_address);
                handle.destroy();
            }

            ~async_router() override = default;
        };

        /**
         * @brief Mandatory promise type fields
         */
        struct mandatory_locals {
            runner_router_slot_t   _runner_router {};        ///< In-place router slot.  Set by the awaited future; read by the runner.
            omni_runner            _runner {nullptr};     ///< Pointer to the owning runner's MPSC task queue.  Set by @c runner::attach().
            // NOTE: Router to manage promise on suspended state.
            // NOTE: Context owns only one promise. Extra slot object is unnecessary
            std::optional<async_router> _self_router;
            bool _roaming { false };
            bool _polling { false };
        };

        /**
         * @brief Mandatory promise type fields and extra fields for lazy coroutines
         */
        struct lazy_locals : mandatory_locals { std::shared_ptr<runner_pool_t> _waiters; };

        /**
         * @brief Mandatory promise type fields and extra fields for lazy and automaton coroutines
         */
        struct automaton_locals : lazy_locals { omni_node _yield_waiter; };

        /**
         * @brief All possible promise type fields for coroutines
         */
        struct full_locals : automaton_locals { };

        typedef
            std::conditional_t<std::same_as<rule_t, lazy_rule<returnT>>,
                lazy_locals,
            std::conditional_t<std::same_as<rule_t, eager_rule<returnT>>,
                mandatory_locals,
            std::conditional_t<std::same_as<rule_t, automaton_rule<returnT>>,
                automaton_locals,
                full_locals
        >>> promise_locals;

        /**
         * @brief C++20 promise type for @c async<returnT, promise_rule_t>.
         *
         * @details Inherits return-value machinery and @c await_transform
         * overloads from @c promise_traits<returnT>.  The concrete fields held
         * by this type (in declaration order, cache-line optimised) are:
         *
         *  | Field | Type | Purpose |
         *  |---|---|---|
         *  | @c _runner_router | @c runner_router_slot_t | In-place storage for the active router. |
         *  | @c _runner | @c omni_runner | Pointer to the owning runner / runner pool. |
         *  | @c _waiters | @c shared_ptr<runner_pool_t> | Queue of asyncs waiting for this one to finish. |
         *  | @c _self_router | @c optional<async_router> | Router installed into the control block. |
         *  | @c _roaming | @c bool | When @c true the balancer may migrate the task to another runner. |
         *  | @c _polling | @c bool | When @c true the runner holds it in low priority task pool. |
         */
        struct promise_type : traits::promise_traits<promise_type, promise_rule_t, returnT>, promise_locals {
            DECLARE_PROMISE_TRAITS(promise_type, promise_rule_t, returnT)
            IMPORT_PROMISE_TRAITS_ENV

            promise_type() = default;

            ~promise_type() = default;

            /**
             * @brief C++20 protocol — initial suspension point.
             * @return @c std::suspend_always for @c ace::async (lazy), or
             *         @c std::suspend_never for @c ace::promise (eager).
             */
            [[nodiscard]] auto initial_suspend() noexcept {
                // NOTE: Fetching runner ptr
                promise_locals::_runner = get_current_pool();
                return rule_t::initial_result();
            }

            /**
             * @brief C++20 protocol — final suspension point.
             * @return @c std::suspend_always — coroutine frame is kept alive
             *         until explicitly destroyed.
             */
            static auto final_suspend() noexcept {
                return std::suspend_always{};
            }

            /**
             * @brief Called by the coroutine machinery when an exception
             *        escapes the coroutine body.
             * @details Sets status to @c e_failed and prints the error.
             */
            void unhandled_exception() {
                status(e_failed);
                interrupt("Unhandled exception.");
            }

            /**
             * @brief Print an error message and call @c final_suspend().
             * @param str  Error message.
             */
            void interrupt(const std::string_view &&str) const {
                std::cerr << str << std::endl;
                final_suspend();
            };

            /**
             * @brief C++20 protocol — construct the return object.
             * @return A @c async that wraps the coroutine handle for this
             *         promise.
             */
            auto get_return_object() noexcept { return async{coroutine_t::from_promise(*this)}; }

            /**
             * @brief C++20 protocol — fallback when allocation fails.
             * @return A default-constructed (null) @c async.
             */
            static auto get_return_object_on_allocation_failure() { return async(nullptr); }

            /**
             * @brief Lazily initialise the control block for external observation.
             *
             * @details Retrieves the @c control_block prefix allocated before
             * this promise, constructs a @c async_router, and links them so
             * that @c control_block_handle::cancel() / @c redirect() work.
             *
             * Only available for lazy (@c differed) coroutines because eager
             * coroutines may already be running by the time @c observe() is
             * called.
             *
             * @tparam promise_t  Promise type of the coroutine handle.
             * @param self  Handle to the owning coroutine.
             */
            template <typename promise_t>
            void setup_control_block(const std::coroutine_handle<promise_t>& self) {
                if (_block) return;
                // NOTE: Getting control block address
                _block = control_block::get_block_from_address(self.address());
                // NOTE: Initiating promise router
                promise_locals::_self_router = async_router(self);
                // NOTE: Passing reference of the inited router to the control block
                _block->_control_router = &promise_locals::_self_router.value();
            }

            /**
             * @brief Construct a @c async_router and return a pointer to it.
             * @details Used when a control router is needed without attaching
             * it to the control block immediately.
             * @tparam promise_t  Promise type of the coroutine handle.
             * @param self  Handle to the owning coroutine.
             * @return Pointer to the newly created router (lifetime tied to
             *         @c _self_router).
             */
            template <typename promise_t>
            traits::async_router_handle* get_control_router(const std::coroutine_handle<promise_t>& self) {
                // NOTE: Initiating promise router
                promise_locals::_self_router = async_router(self);
                return &promise_locals::_self_router.value();
            }

            promise_locals _locals;
        };

        // -----------------------------------------------------------------------
        // Awaitable interface (busy_future_traits — used when nested co_await)
        // -----------------------------------------------------------------------

        /**
         * @brief C++20 awaitable protocol — check if coroutine is already done.
         * @return @c true if the coroutine has finished and the outer coroutine
         *         should not suspend.
         */
        bool await_ready() override {
            if (_coroutine.done()) return true;
            if (_coroutine.promise().status() == e_canceled) return true;
            // NOTE: Necessary to create execution gap to read yield value from calling side
            if (_coroutine.promise().status() == e_executed_with_value) {
                _coroutine.promise().status(e_executed);
                return true;
            }
            if (is_resumable()) {
                release_future();
                _coroutine.resume();
                propagate();
                return _coroutine.done();
            }
            return false;
        }

        /**
         * @brief C++20 awaitable protocol — suspend the outer coroutine.
         * @details On the first call (status @c e_inited), propagates the runner
         * pool pointer from the outer promise.  In all cases, steals the
         * router slot from the inner promise so the runner can find it.
         * @tparam promiseT  Promise type of the outer coroutine.
         * @param outer      Handle to the outer (calling) coroutine.
         * @return @c false if the inner coroutine finished synchronously (outer
         *         should not suspend); @c true otherwise.
         */
        template<typename promiseT>
        bool await_suspend(std::coroutine_handle<promiseT> outer) {
            // NOTE: Secure if _runner is null
            _coroutine.promise()._runner = outer.promise()._runner;
            setup_outer(outer);
            propagate();
            return true;
        }

        /**
         * @brief C++20 awaitable protocol — extract the return value.
         * @return The value stored in @c promise_type::_return_value, or
         *         nothing for @c void coroutines.
         */
        returnT await_resume() {
            if constexpr (requires(promise_type promise_t) { promise_t._return_value; })
                return std::forward<returnT>(_coroutine.promise()._return_value);
            else return;
        }

        // -----------------------------------------------------------------------
        // Runner interface
        // -----------------------------------------------------------------------

        /**
         * @brief Resume the coroutine from the runner.
         *
         * @details Checks whether the coroutine is in a resumable state, clears
         * the current future binding, and calls @c _coroutine.resume().
         * The lifecycle status after the resume is written to @c *_res if the
         * pointer is non-null.
         *
         * @param _res  Optional output pointer that receives the
         *              @c promise_touch_result value after the resume.
         * @return The return value of the coroutine (only meaningful for
         *         non-@c void types after @c e_finished).
         */
        returnT awake(promise_lifecycle *const _res = nullptr) noexcept {
            // NOTE: Checking if promise is ready
            const bool is_ready {
                is_exist()
                and _coroutine.promise().status() not_eq e_failed
                and _coroutine.promise().status() not_eq e_finished
                and _coroutine.promise().status() not_eq e_canceled
                and is_resumable()
            };
            // NOTE: Releasing future and resume async
            if (is_ready) {
                release_future();
                _coroutine.resume();
            }
            // NOTE: For user provided touch result ptr
            if (_res != nullptr) [[likely]]
                *_res = _coroutine.promise().status();
            if constexpr (not std::same_as<void, returnT>)
                return this->_coroutine.promise()._return_value;
            else return;
        }

    };

}

namespace ace {

    // NOTE: Type alias for any type of coroutines (default: lazy)
    template<typename returnT =void, template <typename> typename promise_rule_t = core::lazy_rule>
    requires core::is_rule<promise_rule_t>
    using async = core::async<returnT, promise_rule_t>;

    // NOTE: Type alias for eager coroutines
    template<typename returnT =void>
    using promise = core::async<returnT, core::eager_rule>;

    // NOTE: Type alias for lazy generators
    template<typename returnT =void>
    using automaton = core::async<returnT, core::automaton_rule>;

    // NOTE: Type alias for runner task coroutines
    using task = core::async<>;

    // NOTE: Wrapper to spawn and manage coroutines in runner pool
    template <typename async_return_t, template <typename> typename async_rule_t>
    task task_wrap(core::async<async_return_t, async_rule_t> some_context) {
        co_await some_context;
        co_return;
    }

    // NOTE: Type of a pool for runner [Relates 'async' and 'runner']
    typedef task::runner_pool_t runner_pool_t;

    // NOTE: Type of a pool for task insertion [Relates 'async' and 'runner']
    typedef task::insert_pool_t insert_pool_t;

    // NOTE: Common transfer entity for the async task
    typedef task::omni_node omni_node;

    // NOTE: Unified runner access ptr
    typedef task::omni_runner omni_runner;

    // NOTE: Type of a router handler for runner and future objects [Relates 'future' and 'runner']
    typedef task::runner_router runner_router;

    // NOTE: Type alias for std standard type
    typedef std::suspend_always suspend;
}

// NOTE: raider — an interface proxy for a task that enables multiple-wait,
// pre-captures the future object's resource so it can be quickly returned
// to the object on cancellation

#endif // ACE_ASYNC_H
