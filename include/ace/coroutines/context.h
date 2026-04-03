/**
 * @file context.h
 * @brief Core coroutine context type (`ace::coroutines::context<T, Rule>`)
 *        and the public `ace::async<T>` / `ace::promise<T>` aliases.
 *
 * @details This file defines the central type of the ACE framework.
 * `context<returnT, promise_rule_t>` is a C++20 coroutine type that:
 *
 *  - Inherits from `busy_future_traits<context>` so that it can be directly
 *    `co_await`-ed from another coroutine (nested coroutines).
 *  - Owns its `std::coroutine_handle` and destroys it on destruction.
 *  - Carries a `runner_conductor_slot_t` that futures use to redirect the
 *    context into their own waiting structures (conductor pattern).
 *  - Can expose a `control_block_handle` via `observe()` for external
 *    join / cancel operations.
 *
 * ### Public aliases
 *
 * ```cpp
 * // Lazy coroutine — suspends at creation (initial_suspend = suspend_always)
 * template<typename T = void> using ace::async   = context<T, differed>;
 *
 * // Eager coroutine — runs immediately (initial_suspend = suspend_never)
 * template<typename T = void> using ace::promise = context<T, permanent>;
 * ```
 *
 * @see ace::async, ace::promise, ace::coroutines::promise_traits,
 *      ace::coroutines::control_block
 */
#ifndef ACE_ASYNC_H
#define ACE_ASYNC_H

#include "ace/futures/future.h"
#include "ace/coroutines/promise.h"
#include "nukes/dynamic/mpsc_queue.h"
#include <coroutine>
#include <expected>
#include <iostream>

#include "conduction.h"
#include "control.h"
#include "ace/common/terms.h"


// ToDo: yield операцию надо преретащить в генератор,
// пусть генератор имеет перегрузку итераторов,
// чтобы запускать его в цикле for
namespace ace::coroutines {

    /**
     * @brief Core coroutine context type.
     *
     * @details `context<returnT, promise_rule_t>` represents a single
     * coroutine instance.  It is move-only (copy is deleted) and owns the
     * underlying `std::coroutine_handle`.
     *
     * The type itself satisfies the `busy_future_traits` concept so that one
     * context can be directly `co_await`-ed inside another, enabling nested
     * coroutines.
     *
     * @tparam returnT        Value type returned by `co_return`.
     *                        Defaults to `void`.
     * @tparam promise_rule_t Suspension policy tag.
     *                        Use `differed` (default) for lazy execution or
     *                        `permanent` for eager execution.
     *
     * @see ace::async, ace::promise
     */
    template<typename returnT =void, is_promise_rule promise_rule_t =differed>
    struct context : futures::busy_future_traits<context<returnT, promise_rule_t>> {

        IMPORT_BUSY_FUTURE_ENV(context)

        struct promise_type;

        typedef std::coroutine_handle<promise_type> coroutine_t;           ///< Type of the underlying coroutine handle.

        typedef nukes::dynamic::mpsc_queue<context<>> runner_pool_t;       ///< MPSC queue type used as the runner's task pool.

        typedef runner_conductor_handle<context<>> runner_conductor;       ///< Abstract conductor interface for this context type.

        /// @brief In-place storage slot for a conductor object.
        typedef conductor_slot<runner_conductor> runner_conductor_slot_t;

        coroutine_t _coroutine; ///< Underlying coroutine handle.  Null after move.

        context() = default;

        /**
         * @brief Move constructor.  Transfers ownership of the coroutine handle.
         * @param ctx  Source context.  Its `_coroutine` is set to null.
         */
        context(context && ctx) noexcept {
            _coroutine = std::forward<coroutine_t>(ctx._coroutine);
            ctx._coroutine = nullptr;
        };

        /**
         * @brief Move assignment.  Transfers ownership of the coroutine handle.
         * @param ctx  Source context.  Its `_coroutine` is set to null.
         * @return Reference to `*this`.
         */
        context &operator=(context && ctx)  noexcept {
            _coroutine = std::forward<coroutine_t>(ctx._coroutine);
            ctx._coroutine = nullptr;
            return *this;
        };

        context(const context &) = delete;             ///< Contexts are move-only.
        context &operator=(const context &) = delete;  ///< Contexts are move-only.

        /**
         * @brief Construct from a raw coroutine handle.
         * @details Used internally by `promise_type::get_return_object()`.
         * @param handler  Coroutine handle to take ownership of.
         */
        explicit context(coroutine_t &&handler) : _coroutine{handler} {};

        /**
         * @brief Check whether the coroutine can be resumed.
         * @details Returns `true` iff all of the following hold:
         *  - The handle is non-null.
         *  - The coroutine has not finished (`!done()`).
         *  - The control block has not been disowned (not cancelled).
         * @return `true` if resumable.
         */
        [[nodiscard]] bool is_resumable() const noexcept {
            return _coroutine and not _coroutine.done() and not control_block::is_disowned(_coroutine.address());
        }

        /// @brief Equivalent to `is_resumable()`.
        explicit operator bool() const { return is_resumable(); }

        /**
         * @brief Destructor.  Wakes all registered waiters then destroys the
         *        coroutine frame.
         */
        ~context() override {
            if (_coroutine) {
                release_waiters();
                _coroutine.destroy();
            }
        };

        /**
         * @brief Release the currently-held future and clear the busy-future pointer.
         * @details Called by the runner before resuming a context that was
         * previously forwarded by a conductor.
         */
        void release_future() {
            _coroutine.promise()._runner_conductor.release();
            _coroutine.promise()._busy_future = nullptr;
        }

        /**
         * @brief Check whether the context is ready to be resumed by the runner.
         * @details Returns `true` if no busy future is pending, or if the
         * pending busy future has become ready (`await_ready()` returns `true`).
         * @return `true` if the runner may resume this context.
         */
        bool accessed_by_future() {
            return not _coroutine.promise()._busy_future
                or _coroutine.promise()._busy_future->await_ready();
        }

        /**
         * @brief Create a `control_block_handle` that allows external
         *        join / cancel operations on this coroutine.
         * @details Lazily initializes the control block and the internal
         * `context_conductor`.  Safe to call multiple times; subsequent calls
         * return handles that share the same underlying block.
         * @return A new `control_block_handle` with an incremented weak ref-count.
         * @note The context must not have been moved away before calling `observe()`.
         */
        control_block_handle observe() {
            // NOTE: Setting up promise block by coroutine
            _coroutine.promise().setup_control_block(_coroutine);
            return control_block_handle{ _coroutine };
        }

        /**
         * @brief Wake all coroutines that are waiting for this context to finish.
         * @details Drains the `_waiters` queue and re-attaches each context to
         * its own runner pool.  Called automatically from the destructor.
         */
        void release_waiters() {
#if defined(_MSC_VER)
            if (context<> waiter; _coroutine.promise()._waiters.load()) {
                while (_coroutine.promise()._waiters.load()->pop(waiter)) {
#elif defined(__GNUC__)
            if (context<> waiter; _coroutine.promise()._waiters.load()) {
                while (_coroutine.promise()._waiters.load()->pop(waiter)) {
#elif defined(__clang__)
            if (context<> waiter; std::atomic_load(&_coroutine.promise()._waiters)) {
                while (std::atomic_load(&_coroutine.promise()._waiters)->pop(waiter)) {
#endif
                    waiter.release_future();
                    waiter._coroutine.promise()._runner_pool->push(std::move(waiter));
                }
            }
        }

        /**
         * @brief Allocate a debug trace ID for this context.
         * @details The trace ID is unique for the lifetime of the context and
         * can be used to correlate log entries across asynchronous boundaries.
         * @return The allocated trace ID on success, or an error string if the
         *         coroutine handle is null.
         */
        std::expected<std::size_t, std::string_view> track() {
            if (_coroutine)
                return _coroutine.promise().setup_trace();
            return std::unexpected("context is already dead.");
        }

        class context_conductor : public control_conductor_handle {

            void* _address { nullptr };

        public:

            context_conductor() = default;

            explicit context_conductor(const coroutine_t& coroutine)
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
                auto* waiter = static_cast<context<>*>(undefined_waiter);
#if defined(_MSC_VER)
                handle.promise()._waiters.store(std::make_shared<runner_pool_t>(), std::memory_order_release);
                handle.promise()._waiters.load(std::memory_order_acquire)->push(std::forward<context<>>(*waiter));
#elif defined(__GNUC__)
                handle.promise()._waiters.store(std::make_shared<runner_pool_t>(), std::memory_order_release);
                handle.promise()._waiters.load(std::memory_order_acquire)->push(std::forward<context<>>(*waiter));
#elif defined(__clang__)
                std::atomic_store(&handle.promise()._waiters, std::make_shared<runner_pool_t>());
                std::atomic_load(&handle.promise()._waiters)->push(std::forward<context<>>(*waiter));
#endif
                return true;
            }

            ~context_conductor() override = default;
        };

        /**
         * @brief C++20 promise type for `context<returnT, promise_rule_t>`.
         *
         * @details Inherits return-value machinery and `await_transform`
         * overloads from `promise_traits<returnT>`.  The concrete fields held
         * by this type (in declaration order, cache-line optimised) are:
         *
         *  | Field | Type | Purpose |
         *  |---|---|---|
         *  | `_runner_conductor` | `runner_conductor_slot_t` | In-place storage for the active conductor. |
         *  | `_runner_pool` | `runner_pool_t*` | Pointer to the owning runner's task queue. |
         *  | `_waiters` | atomic `shared_ptr<runner_pool_t>` | Queue of contexts waiting for this one to finish. |
         *  | `_self_conductor` | `optional<context_conductor>` | Conductor installed into the control block. |
         *  | `_roaming` | `bool` | When `true` the balancer may migrate the task to another runner. |
         */
        struct promise_type : promise_traits<returnT> {
            DECLARE_PROMISE_TRAITS(returnT)
            IMPORT_PROMISE_TRAITS_ENV

            promise_type() = default;

            ~promise_type() = default;

            /**
             * @brief C++20 protocol — initial suspension point.
             * @return `std::suspend_always` for `ace::async` (lazy), or
             *         `std::suspend_never` for `ace::promise` (eager).
             */
            [[nodiscard]] auto initial_suspend() const noexcept {
                return promise_rule_t::action();
            }

            /**
             * @brief C++20 protocol — final suspension point.
             * @details Decrements the strong reference count of the control
             * block (if any) before suspending.  The coroutine frame is not
             * destroyed here; the runner or owning `context` destructor does
             * that.
             * @return `std::suspend_always` — coroutine frame is kept alive
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
             * @details Sets status to `e_failed` and prints the error.
             */
            void unhandled_exception() {
                _status = e_failed;
                interrupt("Unhandled exception.");
            }

            /**
             * @brief Print an error message and call `final_suspend()`.
             * @param str  Error message.
             */
            void interrupt(const std::string_view &&str) const {
                std::cerr << str << std::endl;
                final_suspend();
            };

            /**
             * @brief C++20 protocol — construct the return object.
             * @return A `context` that wraps the coroutine handle for this
             *         promise.
             */
            auto get_return_object() noexcept { return context{coroutine_t::from_promise(*this)}; }

            /**
             * @brief C++20 protocol — fallback when allocation fails.
             * @return A default-constructed (null) `context`.
             */
            static auto get_return_object_on_allocation_failure() { return context(nullptr); }

            /**
             * @brief Lazily initialise the control block for external observation.
             *
             * @details Retrieves the `control_block` prefix allocated before
             * this promise, constructs a `context_conductor`, and links them so
             * that `control_block_handle::cancel()` / `forward()` work.
             *
             * Only available for lazy (`differed`) coroutines because eager
             * coroutines may already be running by the time `observe()` is
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
                _self_conductor = context_conductor(self);
                // NOTE: Passing reference of the inited conductor to the control block
                _block->_control_conductor = &_self_conductor.value();
            }

            /**
             * @brief Construct a `context_conductor` and return a pointer to it.
             * @details Used when a control conductor is needed without attaching
             * it to the control block immediately.
             * @tparam promise_t  Promise type of the coroutine handle.
             * @param self  Handle to the owning coroutine.
             * @return Pointer to the newly created conductor (lifetime tied to
             *         `_self_conductor`).
             */
            template <typename promise_t>
            control_conductor_handle* get_promise_conductor(const std::coroutine_handle<promise_t>& self) {
                // NOTE: Initiating promise conductor
                _self_conductor = context_conductor(self);
                return &_self_conductor.value();
            }

            // NOTE: Order of the following variables is optimized. DO NOT SWAP THEM!!!

            runner_conductor_slot_t _runner_conductor {}; ///< In-place conductor slot.  Set by the awaited future; read by the runner.
            runner_pool_t* _runner_pool {nullptr};         ///< Pointer to the owning runner's MPSC task queue.  Set by `runner::attach()`.
#if defined(_MSC_VER)
            std::atomic<std::shared_ptr<runner_pool_t>> _waiters;
#elif defined(__GNUC__)
            std::atomic<std::shared_ptr<runner_pool_t>> _waiters;
#elif defined(__clang__)
            // NOTE: std::atomic<std::shared_ptr<T>> not available on Apple libc++, using free functions
            std::shared_ptr<runner_pool_t> _waiters;
#endif
            // NOTE: Conductor to manage promise on suspended state.
            // NOTE: Context owns only one promise. Extra slot object is unnecessary
            std::optional<context_conductor> _self_conductor;
            alignas(ACE_BUS_SIZE) bool _roaming { false };
        };

        // -----------------------------------------------------------------------
        // Awaitable interface (busy_future_traits — used when nested co_await)
        // -----------------------------------------------------------------------

        /**
         * @brief Internal helper for `await_ready()`.
         * @details Resumes the coroutine inline if it is ready and no future
         * is blocking it.
         * @return `true` if the coroutine finished synchronously.
         */
        bool await_ready_impl() {
            if (_coroutine.done()) return true;
            // NOTE: Checking future to be waited
            if (accessed_by_future()) {
                _coroutine.resume();
                return _coroutine.done();
            }
            return false;
        }

        /**
         * @brief C++20 awaitable protocol — check if coroutine is already done.
         * @details Forces `await_suspend` processing on first call (when status
         * is `e_inited`) so the runner pool pointer can be propagated.
         * @return `true` if the coroutine has finished and the outer coroutine
         *         should not suspend.
         */
        bool await_ready() override {
            // NOTE: Forcing await_suspend processing to define runner_pool
            if (_coroutine.promise()._status == e_inited) return false;
            return await_ready_impl();
        }

        /**
         * @brief C++20 awaitable protocol — suspend the outer coroutine.
         * @details On the first call (status `e_inited`), propagates the runner
         * pool pointer from the outer promise.  In all cases, steals the
         * conductor slot from the inner promise so the runner can find it.
         * @tparam promiseT  Promise type of the outer coroutine.
         * @param outer      Handle to the outer (calling) coroutine.
         * @return `false` if the inner coroutine finished synchronously (outer
         *         should not suspend); `true` otherwise.
         */
        template<typename promiseT>
        bool await_suspend(std::coroutine_handle<promiseT> outer) {
            if (_coroutine.promise()._status == e_inited) {
                _coroutine.promise()._runner_pool = outer.promise()._runner_pool;
                // NOTE: Extra call of await_ready because it was skipped by initial state guard
                if (await_ready_impl()) return false;
            }
            // NOTE: No extra checks needed, because function would be called once before suspending.
            // NOTE: Just coping conductor ptr. Outer task will destroy conductor before current promise stack
            outer.promise()._runner_conductor << _coroutine.promise()._runner_conductor;
            return true;
        }

        /**
         * @brief C++20 awaitable protocol — extract the return value.
         * @return The value stored in `promise_type::_return_value`, or
         *         nothing for `void` coroutines.
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
         * the current future binding, and calls `_coroutine.resume()`.
         * The lifecycle status after the resume is written to `*_res` if the
         * pointer is non-null.
         *
         * @param _res  Optional output pointer that receives the
         *              `promise_touch_result` value after the resume.
         * @return The return value of the coroutine (only meaningful for
         *         non-`void` types after `e_finished`).
         */
        returnT awake(promise_touch_result *const _res = nullptr) noexcept {
            // NOTE: Checking if promise is ready
            const bool is_ready {
                is_resumable()
                and _coroutine.promise()._status not_eq e_failed
                and _coroutine.promise()._status not_eq e_finished
                and _coroutine.promise()._status not_eq e_detached
                and accessed_by_future()
            };
            // NOTE: Releasing future and resume context
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

    // NOTE: Type alias for greedy coroutines
    template<typename returnT =void>
    using promise = coroutines::context<returnT, coroutines::permanent>;

    // NOTE: Type alias for lazy coroutines
    template<typename returnT =void>
    using async = coroutines::context<returnT>;

    // NOTE: Wrapper to spawn and manage coroutines in runner pool
    template <typename async_return_t>
    async<> async_wrap(async<async_return_t>&& some_async) {
        co_await some_async;
        co_return;
    }

    // NOTE: Type of a pool for runner [Relates 'context' and 'runner']
    typedef async<>::runner_pool_t runner_pool_t;

    // NOTE: Type of a conductor handler for runner and future objects [Relates 'future' and 'runner']
    typedef async<>::runner_conductor conductor_handler_t;

    // NOTE: Type alias for std standard type
    typedef std::suspend_always suspend;
}

// Говно нахуй не нужное, но
// bridge - обобщенный канал для отслеживания состояний запущеных в параллель асинхронных компонент, и отправки управляющих сигналов
// raider - интерфейсный заместитель таски для множественного ожидания, предзахватывает ресурс future объектов, чтобы его можно было быстро вернуть в объект при отмене
// *detach - spawn вызов, но для future, с бриджом сигнализации или консьюмером, который поглотит результат future, удобно для мелких операций на которых не хочется останавливаться, работает через запуск рейдера

// Добавить defer stack и insure stack

#endif // ACE_ASYNC_H
