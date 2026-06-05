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
 *  - Carries a @c runner_conductor_slot_t that futures use to redirect the
 *    async into their own waiting structures (conductor pattern).
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
#include <expected>
#include <iostream>

#include <nukes/dynamic/regular_queue.h>
#include <nukes/details/prefetch.h>

#include "ace/core/traits/future.h"
#include "ace/core/traits/promise.h"
#include "ace/core/tools/macro.h"
#include "ace/core/control.h"
#include "ace/core/traits/conduction.h"


// ToDo: yield операцию надо преретащить в генератор,
// пусть генератор имеет перегрузку итераторов,
// чтобы запускать его в цикле for
namespace ace::core {

    struct cast_ptr {

        void* _ptr = nullptr;

        cast_ptr(void* ptr = nullptr) : _ptr{ptr} { };

        template <typename T>
        cast_ptr(T* ptr) : _ptr{ptr} { };

        cast_ptr (const cast_ptr & p) = default;

        cast_ptr (cast_ptr&& p) = default;

        cast_ptr& operator=(const cast_ptr & p) = default;

        cast_ptr& operator=(cast_ptr&& p) = default;

        cast_ptr& operator=(void* ptr) { _ptr = ptr; return *this; }

        explicit operator bool() const noexcept { return _ptr not_eq nullptr; }

        template <typename T>
        [[nodiscard]] auto as() const { return static_cast<T*>(_ptr); }

        template <typename T>
        auto addr_of() { return reinterpret_cast<T**>(&_ptr); }

    };

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
     * @tparam promise_rule_t Suspension policy tag.
     *                        Use @c differed (default) for lazy execution or
     *                        @c permanent for eager execution.
     *
     * @see ace::async, ace::promise
     */
    template<typename returnT =void, is_promise_rule promise_rule_t =differed>
    struct ACE_AWAIT_NODISCARD async : traits::busy_future_traits<async<returnT, promise_rule_t>> {

        IMPORT_BUSY_FUTURE_ENV(async)

        struct promise_type;

        typedef std::coroutine_handle<promise_type> coroutine_t;             ///< Type of the underlying coroutine handle.
        typedef nukes::dynamic::reg_queue<async<>> runner_pool_t;          ///< Queue type used as the runner's task pool.
        typedef traits::runner_conductor_handle<async<>> runner_conductor; ///< Abstract conductor interface for this async type.

        /// @brief In-place storage slot for a conductor object.
        typedef traits::conductor_slot<runner_conductor> runner_conductor_slot_t;

        coroutine_t _coroutine; ///< Underlying coroutine handle.  Null after move.

        /// @brief Helper to get active runner pool ptr or @c nullptr
        /// if @c async<...> is constructed out of runner context
        static runner_pool_t* get_current_pool() noexcept;

        void setup_runner() {
            // TODO: Why not? But but not works
            // if constexpr (std::same_as<promise_rule_t, permanent>)
            //     if (not _coroutine.promise()._runner_pool)
            //         _coroutine.promise()._runner_pool = get_current_pool();
        }

        async() { setup_runner(); }

        /**
         * @brief Move constructor.  Transfers ownership of the coroutine handle.
         * @param ctx  Source async.  Its @c _coroutine is set to null.
         */
        async(async && ctx) noexcept {
            _coroutine = std::forward<coroutine_t>(ctx._coroutine);
            ctx._coroutine = nullptr;
            setup_runner();
        };

        /**
         * @brief Move assignment.  Transfers ownership of the coroutine handle.
         * @param ctx  Source async.  Its @c _coroutine is set to null.
         * @return Reference to @c *this.
         */
        async &operator=(async && ctx)  noexcept {
            _coroutine = std::forward<coroutine_t>(ctx._coroutine);
            ctx._coroutine = nullptr;
            setup_runner();
            return *this;
        };

        async(const async &) = delete;             ///< Contexts are move-only.
        async &operator=(const async &) = delete;  ///< Contexts are move-only.

        /**
         * @brief Construct from a raw coroutine handle.
         * @details Used internally by @c promise_type::get_return_object().
         * @param handler  Coroutine handle to take ownership of.
         */
        explicit async(coroutine_t &&handler) : _coroutine{handler} { setup_runner(); };

        /**
         * @brief Check whether the coroutine is exist.
         * @details Returns @c true iff all of the following hold:
         *  - The handle is non-null.
         *  - The coroutine has not finished (@c !done()).
         *  - The control block has not been disowned (not cancelled).
         * @return @c true if resumable.
         */
        [[nodiscard]] bool is_exist() const noexcept {
            return _coroutine and not _coroutine.done() and not control_block::is_disowned(_coroutine.address());
        }

        /// @brief Equivalent to @c is_exist().
        explicit operator bool() const { return is_exist(); }

        /**
         * @brief Destructor.  Wakes all registered waiters then destroys the
         *        coroutine frame.
         */
        ~async() override {
            if (_coroutine) {
                release_waiters();
                _coroutine.destroy();
            }
        };

        /**
         * @brief Release the currently-held future and clear the busy-future pointer.
         * @details Called by the runner before resuming a async that was
         * previously forwarded by a conductor.
         */
        void release_future() {
            _coroutine.promise()._runner_conductor.release();
            _coroutine.promise()._busy_future = nullptr;
        }

        /**
         * @brief Check whether the async is ready to be resumed by the runner.
         * @details Returns @c true if no busy future is pending, or if the
         * pending busy future has become ready (@c await_ready() returns @c true).
         * @return @c true if the runner may resume this async.
         */
        bool is_resumable() {
            return (not _coroutine.promise()._busy_future or _coroutine.promise()._busy_future->await_ready())
                    and _coroutine.promise()._runner;
        }

        /**
         * @brief Create a @c control_block_handle that allows external
         *        join / cancel operations on this coroutine.
         * @details Lazily initializes the control block and the internal
         * @c async_conductor.  Safe to call multiple times; subsequent calls
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
            if (_coroutine.promise()._waiters) {
                async<> waiter;
                while (_coroutine.promise()._waiters->pop(waiter)) {
                    waiter.release_future();
                    waiter._coroutine.promise()._runner.as<runner_pool_t>()->push(std::move(waiter));
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

        class async_conductor : public traits::control_conductor_handle {

            void* _address { nullptr };

        public:

            async_conductor() = default;

            explicit async_conductor(const coroutine_t& coroutine)
                : _address(coroutine.address()) {}

            void cancel() noexcept override {
                if (not _address) [[unlikely]] return;
                auto handle = coroutine_t::from_address(_address);
                if (handle and handle.promise()._runner_conductor) [[likely]] {
                    handle.promise()._runner_conductor->cancel();
                    handle.promise()._runner_conductor.release();
                }
                handle.promise()._status = e_detached;
            }

            bool forward(void* undefined_waiter) noexcept override {
                if (not _address or not undefined_waiter) [[unlikely]] return false;
                auto handle = coroutine_t::from_address(_address);
                auto* waiter = static_cast<async<>*>(undefined_waiter);
                handle.promise()._waiters = std::make_shared<runner_pool_t>();
                handle.promise()._waiters->push(std::forward<async<>>(*waiter));
                return true;
            }

            ~async_conductor() override = default;
        };

        /**
         * @brief C++20 promise type for @c async<returnT, promise_rule_t>.
         *
         * @details Inherits return-value machinery and @c await_transform
         * overloads from @c promise_traits<returnT>.  The concrete fields held
         * by this type (in declaration order, cache-line optimised) are:
         *
         *  | Field | Type | Purpose |
         *  |---|---|---|
         *  | @c _runner_conductor | @c runner_conductor_slot_t | In-place storage for the active conductor. |
         *  | @c _runner_pool | @c runner_pool_t* | Pointer to the owning runner's task queue. |
         *  | @c _waiters | @c shared_ptr<runner_pool_t> | Queue of asyncs waiting for this one to finish. |
         *  | @c _self_conductor | @c optional<async_conductor> | Conductor installed into the control block. |
         *  | @c _roaming | @c bool | When @c true the balancer may migrate the task to another runner. |
         *  | @c _polling | @c bool | When @c true the runner holds it in low priority task pool. |
         */
        struct promise_type : traits::promise_traits<promise_type, returnT> {
            DECLARE_PROMISE_TRAITS(promise_type, returnT)
            IMPORT_PROMISE_TRAITS_ENV

            promise_type() = default;

            ~promise_type() = default;

            /**
             * @brief C++20 protocol — initial suspension point.
             * @return @c std::suspend_always for @c ace::async (lazy), or
             *         @c std::suspend_never for @c ace::promise (eager).
             */
            [[nodiscard]] auto initial_suspend() const noexcept {
                return promise_rule_t::action();
            }

            /**
             * @brief C++20 protocol — final suspension point.
             * @details Decrements the strong reference count of the control
             * block (if any) before suspending.  The coroutine frame is not
             * destroyed here; the runner or owning @c async destructor does
             * that.
             * @return @c std::suspend_always — coroutine frame is kept alive
             *         until explicitly destroyed.
             */
            auto final_suspend() const noexcept {
                // NOTE: Decreasing strong counter on finish
                if (_block) control_block::disown(_block);
                return std::suspend_always{};
            }

            /**
             * @brief Called by the coroutine machinery when an exception
             *        escapes the coroutine body.
             * @details Sets status to @c e_failed and prints the error.
             */
            void unhandled_exception() {
                _status = e_failed;
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
             * this promise, constructs a @c async_conductor, and links them so
             * that @c control_block_handle::cancel() / @c forward() work.
             *
             * Only available for lazy (@c differed) coroutines because eager
             * coroutines may already be running by the time @c observe() is
             * called.
             *
             * @tparam promise_t  Promise type of the coroutine handle.
             * @param self  Handle to the owning coroutine.
             */
            template <typename promise_t>
            requires std::same_as<differed, promise_rule_t>
            void setup_control_block(const std::coroutine_handle<promise_t>& self) {
                // NOTE: Getting control block address
                _block = control_block::get_block_from_address(self.address());
                // NOTE: Initiating promise conductor
                _self_conductor = async_conductor(self);
                // NOTE: Passing reference of the inited conductor to the control block
                _block->_control_conductor = &_self_conductor.value();
            }

            /**
             * @brief Construct a @c async_conductor and return a pointer to it.
             * @details Used when a control conductor is needed without attaching
             * it to the control block immediately.
             * @tparam promise_t  Promise type of the coroutine handle.
             * @param self  Handle to the owning coroutine.
             * @return Pointer to the newly created conductor (lifetime tied to
             *         @c _self_conductor).
             */
            template <typename promise_t>
            traits::control_conductor_handle* get_promise_conductor(const std::coroutine_handle<promise_t>& self) {
                // NOTE: Initiating promise conductor
                _self_conductor = async_conductor(self);
                return &_self_conductor.value();
            }

            // NOTE: Order of the following variables is optimized. DO NOT SWAP THEM!!!

            runner_conductor_slot_t _runner_conductor {};  ///< In-place conductor slot.  Set by the awaited future; read by the runner.
            cast_ptr _runner {nullptr};                    ///< Pointer to the owning runner's MPSC task queue.  Set by @c runner::attach().
            std::shared_ptr<runner_pool_t> _waiters;
            // NOTE: Conductor to manage promise on suspended state.
            // NOTE: Context owns only one promise. Extra slot object is unnecessary
            std::optional<async_conductor> _self_conductor;
            bool _roaming { false };
            bool _polling { false };
        };

        // -----------------------------------------------------------------------
        // Awaitable interface (busy_future_traits — used when nested co_await)
        // -----------------------------------------------------------------------

        /**
         * @brief C++20 awaitable protocol — check if coroutine is already done.
         * @details Forces @c await_suspend processing on first call (when status
         * is @c e_inited) so the runner pool pointer can be propagated.
         * @return @c true if the coroutine has finished and the outer coroutine
         *         should not suspend.
         */
        bool await_ready() override {
            if (_coroutine.done()) return true;
            if (is_resumable()) {
                _coroutine.resume();
                return _coroutine.done();
            }
            return false;
        }

        /**
         * @brief C++20 awaitable protocol — suspend the outer coroutine.
         * @details On the first call (status @c e_inited), propagates the runner
         * pool pointer from the outer promise.  In all cases, steals the
         * conductor slot from the inner promise so the runner can find it.
         * @tparam promiseT  Promise type of the outer coroutine.
         * @param outer      Handle to the outer (calling) coroutine.
         * @return @c false if the inner coroutine finished synchronously (outer
         *         should not suspend); @c true otherwise.
         */
        template<typename promiseT>
        bool await_suspend(std::coroutine_handle<promiseT> outer) {
            // NOTE: Secure if _runner_pool is null
            if (not _coroutine.promise()._runner)
                _coroutine.promise()._runner = outer.promise()._runner;
            // NOTE: Extra call of await_ready fore differed async because it was skipped by idle runner pool ptr
            if (_coroutine.promise()._status == e_inited)
                if (await_ready()) return false;
            // NOTE: No extra checks needed, because function would be called once before suspending.
            // NOTE: Just coping conductor ptr. Outer task will destroy conductor before current promise stack
            outer.promise()._runner_conductor << _coroutine.promise()._runner_conductor;
            return true;
        }

        /**
         * @brief C++20 awaitable protocol — extract the return value.
         * @return The value stored in @c promise_type::_return_value, or
         *         nothing for @c void coroutines.
         */
        returnT await_resume() {
            if constexpr (requires(promise_type promise_t) { promise_t._return_value; })
                return _coroutine.promise()._return_value;
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
        returnT awake(promise_touch_result *const _res = nullptr) noexcept {
            // NOTE: Checking if promise is ready
            const bool is_ready {
                is_exist()
                and _coroutine.promise()._status not_eq e_failed
                and _coroutine.promise()._status not_eq e_finished
                and _coroutine.promise()._status not_eq e_detached
                and is_resumable()
            };
            // NOTE: Releasing future and resume async
            if (is_ready) {
                release_future();
                _coroutine.resume();
            }
            // NOTE: For user provided touch result ptr
            if (_res != nullptr) [[likely]]
                *_res = _coroutine.promise()._status;
            if constexpr (not std::same_as<void, returnT>)
                return this->_coroutine.promise()._return_value;
            else return;
        }

    };

}

namespace ace {

    // NOTE: Type alias for any type of coroutines (default: lazy)
    template<typename returnT =void, core::is_promise_rule promise_rule_t = core::differed>
    using async = core::async<returnT, promise_rule_t>;

    // NOTE: Type alias for eager coroutines
    template<typename returnT =void>
    using promise = async<returnT, core::permanent>;

    // NOTE: Type alias for runner task coroutines
    using task = async<>;

    // NOTE: Wrapper to spawn and manage coroutines in runner pool
    template <typename async_return_t, core::is_promise_rule async_rule_t>
    task task_wrap(core::async<async_return_t, async_rule_t>&& some_context) {
        co_await some_context;
        co_return;
    }

    // NOTE: Type of a pool for runner [Relates 'async' and 'runner']
    typedef task::runner_pool_t runner_pool_t;

    // NOTE: Type of a conductor handler for runner and future objects [Relates 'future' and 'runner']
    typedef task::runner_conductor conductor_handler_t;

    // NOTE: Type alias for std standard type
    typedef std::suspend_always suspend;
}

// Говно нахуй не нужное, но
// raider - интерфейсный заместитель таски для множественного ожидания, предзахватывает ресурс future объектов, чтобы его можно было быстро вернуть в объект при отмене

#endif // ACE_ASYNC_H
