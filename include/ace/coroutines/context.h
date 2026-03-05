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

#include "conductor.h"
#include "control.h"
#include "ace/common/terms.h"


// ToDo: yield операцию преретащить в генератор/ пусть генератор имеет перегрузку итераторов чтобы запускать его в цикле for
namespace ace::coroutines {

    template<typename returnT =void, is_promise_rule promise_rule_t =differed>
    struct context : futures::future_traits<context<returnT, promise_rule_t>> {
        DECLARE_FUTURE(context)
        IMPORT_FUTURE_ENV

        struct promise_type;

        typedef std::coroutine_handle<promise_type> coroutine_t;

        typedef nukes::dynamic::mpsc_queue<context<>> runner_pool_t;

        typedef conductor_traits<context<>> conductor_handler_t;

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
            _coroutine.promise()._future_conductor.release();
            _coroutine.promise()._future = nullptr;
        }

        control_block_handle observe() {
            // NOTE: Setting up promise block by coroutine
            _coroutine.promise().setup_control_block(_coroutine);
            return control_block_handle{ _coroutine };
        }

        void release_waiters() {
            if (context<> waiter; _coroutine.promise()._waiters.load()) {
                while (_coroutine.promise()._waiters.load()->pop(waiter)) {
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

        // NOTE: Type to store conductor and pass it to outer promise
        struct conductor_carry {
            template <typename conductor_t>
            requires std::derived_from<conductor_t, conductor_handler_t>
            conductor_carry& operator =(const conductor_t& conductor) {
                static_assert(sizeof(conductor_t) <= ACE_CONDUCTOR_MEM_SIZE,
                "[conductor_carry]: conductor size can't be larger than ACE_CONDUCTOR_MEM_SIZE");
                _conductor = new (_conductor_area) conductor_t(std::forward<conductor_t>(conductor));
                return *this;
            }

            template <typename conductor_t>
            requires std::derived_from<conductor_t, conductor_handler_t>
            conductor_carry& operator =(conductor_t&& conductor) {
                static_assert(sizeof(conductor_t) <= ACE_CONDUCTOR_MEM_SIZE,
                "[conductor_carry]: conductor size can't be larger than ACE_CONDUCTOR_MEM_SIZE");
                _conductor = new (_conductor_area) conductor_t(std::forward<conductor_t>(conductor));
                return *this;
            }

            template<typename carry_t>
            requires requires { carry_t::_conductor; carry_t::_conductor_area; }
            conductor_carry& operator <<(carry_t& carry) noexcept {
                if (carry._conductor) {
                    _conductor = carry._conductor;
                    carry._conductor = nullptr;
                }
                return *this;
            }

            // NOTE: Releases conductor from carry with distracting
            void release() {
                if (_conductor) {
                    _conductor->~conductor_handler_t();
                    _conductor = nullptr;
                }
            }

            // NOTE: Wipes conductor data from carry without distracting
            void reset() {
                if (_conductor)
                    _conductor = nullptr;
            }

            [[nodiscard]] conductor_handler_t* get() const { return _conductor; }

            conductor_handler_t* operator->() const { return get(); }

            explicit operator bool() const { return _conductor != nullptr; };

            ~conductor_carry() { release(); };

            conductor_handler_t* _conductor {nullptr};
            alignas(ACE_BUS_SIZE) uint8_t _conductor_area [ACE_CONDUCTOR_MEM_SIZE] {};
        };

        class promise_conductor : public promise_conductor_handle {

            void* _address { nullptr };

        public:

            promise_conductor() = default;

            explicit promise_conductor(const coroutine_t& coroutine)
                : _address(coroutine.address()) {}

            void cancel() noexcept override {
                if (not _address) [[unlikely]] return;
                auto handle = coroutine_t::from_address(_address);
                if (handle and handle.promise()._future_conductor) [[likely]] {
                    handle.promise()._future_conductor->cancel();
                    handle.promise()._future_conductor.release();
                }
                handle.promise()._status = e_detached;
            }

            bool forward(void* undefined_waiter) noexcept override {
                if (not _address or not undefined_waiter) [[unlikely]] return false;
                auto handle = coroutine_t::from_address(_address);
                auto* waiter = static_cast<context<>*>(undefined_waiter);
                handle.promise()._waiters.store(std::make_shared<runner_pool_t>());
                handle.promise()._waiters.load()->push(std::forward<context<>>(*waiter));
                return true;
            }

            ~promise_conductor() override = default;
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
            void setup_control_block(const std::coroutine_handle<promise_t>& c) {
                // NOTE: Getting control block address
                _block = control_block::get_block_from_address(c.address());
                // NOTE: Initiating promise conductor
                _promise_conductor = promise_conductor(c);
                // NOTE: Setting promise conductor
                _block->_promise_conductor = &c.promise()._promise_conductor.value();
            }

            template <typename promise_t>
            promise_conductor_handle* get_promise_conductor(const std::coroutine_handle<promise_t>& c) {
                // NOTE: Initiating promise conductor
                _promise_conductor = promise_conductor(c);
                return &_promise_conductor.value();
            }

            // NOTE: Conductor to manage futures on suspended state.
            // NOTE: Carry object needed because few futures can be awaited during context run
            conductor_carry _future_conductor {};
            // TODO: Wrap into weak hazard ptr, when I will write it
            runner_pool_t* _runner_pool {nullptr};
            std::atomic<std::shared_ptr<runner_pool_t>> _waiters;
            // NOTE: Conductor to manage promise on suspended state.
            // NOTE: Context owns only one promise. Extra carry object is unnecessary
            std::optional<promise_conductor> _promise_conductor;
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
            outer.promise()._future_conductor << _coroutine.promise()._future_conductor;
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
    typedef async<>::conductor_handler_t conductor_handler_t;

    // NOTE: Type alias for std standard type
    typedef std::suspend_always suspend;
}

// Говно нахуй не нужное, но
// bridge - обобщенный канал для отслеживания состояний запущеных в параллель асинхронных компонент, и отправки управляющих сигналов
// raider - интерфейсный заместитель таски для множественного ожидания, предзахватывает ресурс future объектов, чтобы его можно было быстро вернуть в объект при отмене
// *detach - spawn вызов, но для future, с бриджом сигнализации или консьюмером, который поглотит результат future, удобно для мелких операций на которых не хочется останавливаться, работает через запуск рейдера

// Добавить defer stack и insure stack

#endif // ACE_ASYNC_H
