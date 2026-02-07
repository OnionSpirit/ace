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

#include "conductor.h"

#ifndef ACE_CONDUCTOR_MEM_SIZE
#define ACE_CONDUCTOR_MEM_SIZE std::hardware_constructive_interference_size // Size of cacheline
#endif

// ToDo: yield операцию преретащить в генератор/ пусть генератор имеет перегрузку итераторов чтобы запускать его в цикле for
namespace ace::coroutines {

    template<typename returnT =void, is_promise_rule launch_ruleT =differed>
    struct context : futures::future_traits<context<returnT, launch_ruleT>> {
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

        // NOTE: Checks if context is idle
        [[nodiscard]] bool is_idle() const noexcept { return not _coroutine or _coroutine.done(); }

        explicit operator bool() const { return is_idle(); }

        ~context() override { if (_coroutine) _coroutine.destroy(); };


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
            conductor_carry& operator =(carry_t&& carry) noexcept {
                if (carry._conductor) {
                    _conductor = carry._conductor;
                    memcpy(_conductor_area, carry._conductor_area, ACE_CONDUCTOR_MEM_SIZE);
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
            uint8_t _conductor_area [ACE_CONDUCTOR_MEM_SIZE] {};
        };

        struct promise_type : promise_traits<returnT> {
            DECLARE_PROMISE_TRAITS(returnT)
            IMPORT_PROMISE_TRAITS_ENV

            promise_type() = default;

            ~promise_type() = default;

            [[nodiscard]] auto initial_suspend() const noexcept {
                return launch_ruleT::action();
            }

            auto final_suspend() noexcept { return std::suspend_always{}; }

            void unhandled_exception() {
                _status = e_failed;
                interrupt("Unhandled exception.");
            }

            void interrupt(const std::string_view &&str) {
                final_suspend();
            };

            auto get_return_object() noexcept { return context{coroutine_t::from_promise(*this)}; }

            static auto get_return_object_on_allocation_failure() { return context(nullptr); }

            // TODO: Wrap into weak hazard ptr, when I will write it
            runner_pool_t* _runner_pool {nullptr};
            conductor_carry _conductor {};
        };

        bool await_ready() override {
            if (_coroutine.done()) return true;
            if (_coroutine.promise()._future and _coroutine.promise()._future->await_ready())
                _coroutine.promise()._future = nullptr;
            _coroutine.resume();
            return _coroutine.done();
        }

        template<typename promiseT>
        bool await_suspend(std::coroutine_handle<promiseT> outer) {
            // NOTE: Passing conductor to outer
            if (not is_idle() and _coroutine.promise()._conductor)
                outer.promise()._conductor = std::move(_coroutine.promise()._conductor);
            // NOTE: Suspending if not idle
            if (not is_idle())
                return false;
            return true;
        }

        returnT await_resume() {
            if constexpr (requires(promise_type promise_t) { promise_t._return_value; })
                return _coroutine.promise()._return_value;
            else return;
        }

        returnT awake(promise_touch_result *const _res = nullptr) noexcept {
            const bool is_ready {
                not is_idle()
                and (not _coroutine.promise()._future or _coroutine.promise()._future->await_ready())
            };
            // NOTE: Clear future ptr and resume context
            if (is_ready) {
                _coroutine.promise()._future = nullptr;
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
}

// Говно нахуй не нужное, но
// bridge - обобщенный канал для отслеживания состояний запущеных в параллель асинхронных компонент, и отправки управляющих сигналов
// raider - интерфейсный заместитель таски для множественного ожидания, предзахватывает ресурс future объектов, чтобы его можно было быстро вернуть в объект при отмене
// *detach - spawn вызов, но для future, с бриджом сигнализации или консьюмером, который поглотит результат future, удобно для мелких операций на которых не хочется останавливаться, работает через запуск рейдера

// Добавить defer stack и insure stack

#endif // ACE_ASYNC_H
