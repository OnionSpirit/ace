/**
 * @file
 * @details This file contains a future_handler, future_trait classes and its
 * dispatching concepts: is_future_accept_promise, is_future_accept_coroutine,
 * is_future. Types are intended to be used to create derived future objects,
 * that will be processed by co_await operator
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


// ToDo: yield операцию преретащить в генератор/ пусть генератор имеет перегрузку итераторов чтобы запускать его в цикле for
namespace ace::coroutines {

    template<typename returnT =void, is_promise_rule promise_rule_t =differed>
    struct context : futures::busy_future_traits<context<returnT, promise_rule_t>> {

        IMPORT_BUSY_FUTURE_ENV(context)

        struct promise_type;

        typedef std::coroutine_handle<promise_type> coroutine_t;

        typedef nukes::dynamic::mpsc_queue<context<>> runner_pool_t;

        typedef runner_conductor_handle<context<>> runner_conductor;

        // NOTE: Type to store conductor and pass it to outer promise
        typedef conductor_slot<runner_conductor> runner_conductor_slot_t;

        coroutine_t _coroutine;

        context() = default;

        context(context && ctx) noexcept {
            _coroutine = std::forward<coroutine_t>(ctx._coroutine);
            ctx._coroutine = nullptr;
        };

        context &operator=(context && ctx)  noexcept {
            _coroutine = std::forward<coroutine_t>(ctx._coroutine);
            ctx._coroutine = nullptr;
            return *this;
        };

        context(const context &) = delete;

        context &operator=(const context &) = delete;

        explicit context(coroutine_t &&handler) : _coroutine{handler} {};

        // NOTE: Checks if context is resumable
        [[nodiscard]] bool is_resumable() const noexcept {
            return _coroutine and not _coroutine.done() and not control_block::is_disowned(_coroutine.address());
        }

        explicit operator bool() const { return is_resumable(); }

        ~context() override {
            if (_coroutine) {
                release_waiters();
                _coroutine.destroy();
            }
        };

        void release_future() {
            _coroutine.promise()._runner_conductor.release();
            _coroutine.promise()._future = nullptr;
        }

        control_block_handle observe() {
            // NOTE: Setting up promise block by coroutine
            _coroutine.promise().setup_control_block(_coroutine);
            return control_block_handle{ _coroutine };
        }

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

        struct promise_type : promise_traits<returnT> {
            DECLARE_PROMISE_TRAITS(returnT)
            IMPORT_PROMISE_TRAITS_ENV

            promise_type() = default;

            ~promise_type() = default;

            [[nodiscard]] auto initial_suspend() const noexcept {
                return promise_rule_t::action();
            }

            auto final_suspend() const noexcept {
                // NOTE: Decreasing strong counter on finish
                if (_block) control_block::disown(_block);
                return std::suspend_always{};
            }

            void unhandled_exception() {
                _status = e_failed;
                interrupt("Unhandled exception.");
            }

            void interrupt(const std::string_view &&str) const {
                std::cerr << str << std::endl;
                final_suspend();
            };

            auto get_return_object() noexcept { return context{coroutine_t::from_promise(*this)}; }

            static auto get_return_object_on_allocation_failure() { return context(nullptr); }

            // NOTE: Set ups control block settings from extern coroutine_handle object
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

            template <typename promise_t>
            control_conductor_handle* get_promise_conductor(const std::coroutine_handle<promise_t>& self) {
                // NOTE: Initiating promise conductor
                _self_conductor = context_conductor(self);
                return &_self_conductor.value();
            }

            // NOTE: Order of the following variables is optimized. DO NOT SWAP THEM!!!

            // NOTE: Conductor to manage futures on suspended state.
            // NOTE: Slot object needed because few futures can be awaited during context run
            runner_conductor_slot_t _runner_conductor {};
            runner_pool_t* _runner_pool {nullptr};
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

        bool await_ready() override {
            if (_coroutine.done()) return true;
            if (_coroutine.promise()._future and not _coroutine.promise()._future->await_ready())
                return false;
            release_future();
            _coroutine.resume();
            return _coroutine.done();
        }

        template<typename promiseT>
        bool await_suspend(std::coroutine_handle<promiseT> outer) {
            // NOTE: No extra checks needed, because function would be called once before suspending.
            // NOTE: Just coping conductor ptr. Outer task will destroy conductor before current promise stack
            outer.promise()._runner_conductor << _coroutine.promise()._runner_conductor;
            return true;
        }

        returnT await_resume() {
            if constexpr (requires(promise_type promise_t) { promise_t._return_value; })
                return _coroutine.promise()._return_value;
            else return;
        }

        returnT awake(promise_touch_result *const _res = nullptr) noexcept {
            // NOTE: Checking if promise are ready
            const bool is_ready {
                is_resumable()
                and _coroutine.promise()._status not_eq e_failed
                and _coroutine.promise()._status not_eq e_finished
                and _coroutine.promise()._status not_eq e_detached
                and (not _coroutine.promise()._future or _coroutine.promise()._future->await_ready())
            };
            // NOTE: Clear future and resume context
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
