/**
 * @file
 * @details This file contains a future_handler, future_trait classes and its
 * dispatching concepts: is_future_accept_promise, is_future_accept_coroutine,
 * is_future. Types are intended to be used to create derived future objects,
 * that will be processed by co_await operator
 */
#ifndef ACE_CONTEXT_H
#define ACE_CONTEXT_H

#include "ace/futures/future.h"
#include "ace/promises/promise.h"
#include <coroutine>

// ToDo: yield операцию преретащить в генератор/ пусть генератор имеет перегрузку итераторов чтобы запускать его в цикле for
namespace ace::promises {

    template<typename returnT =void, is_promise_rule launch_ruleT =differed>
    struct async : futures::future_traits<async<returnT> > {
        DECLARE_FUTURE(async)
        IMPORT_FUTURE_ENV

        struct promise_type;

        typedef std::coroutine_handle<promise_type> coroutine_t;

        coroutine_t _coroutine;

        async() = default;

        explicit async(coroutine_t &&h) : _coroutine{h} {};

        explicit operator bool() const { return not _coroutine or _coroutine.done(); }

        ~async() override = default;

        struct promise_type : promise_traits<returnT> {
            DECLARE_PROMISE_TRAITS(returnT)
            IMPORT_PROMISE_TRAITS_ENV

            promise_type() = default;

            ~promise_type() = default;

            auto initial_suspend() const noexcept {
                return launch_ruleT::action();
            }

            auto final_suspend() noexcept { return std::suspend_always{}; }

            void unhandled_exception() {
                _status = e_failed;
                this->interrupt("Unhandled exception.");
            }

            void interrupt(const std::string_view &&str) {
                this->final_suspend();
            };

            auto get_return_object() noexcept { return async{coroutine_t::from_promise(*this)}; }

            static auto get_return_object_on_allocation_failure() { return async(nullptr); }
        };

        bool await_ready() override {
            if (_coroutine.done()) return true;
            if (_coroutine.promise()._future)
                if (_coroutine.promise()._future->await_ready())
                    _coroutine.promise()._future = nullptr;
            _coroutine.resume();
            return _coroutine.done();
        }

        template<typename promiseT>
        bool await_suspend(std::coroutine_handle<promiseT>) {
            if (not _coroutine or _coroutine.done())
                return false;
            return true;
        }

        returnT await_resume() {
            if constexpr (requires(promise_type promise_t) { promise_t._return_value; })
                return _coroutine.promise()._return_value;
            else return;
        }

        returnT awake(promise_touch_result *const _res = nullptr) noexcept {
            if (_coroutine and not _coroutine.done()) [[likely]] {
                if (not _coroutine.promise()._future)
                    _coroutine();
                else if (_coroutine.promise()._future->await_ready()) {
                    _coroutine.promise()._future = nullptr;
                    _coroutine();
                }
            }

            if (_res != nullptr)
                *_res = _coroutine.promise()._status;

            if constexpr (not std::same_as<void, returnT>)
                return this->_coroutine.promise()._return_value;
            else return;
        }
    };
}

// Говно нахуй не нужное, но
// bridge - обобщенный канал для отслеживания состояний запущеных в параллель асинхронных компонент, и отправки управляющих сигналов
// raider - интерфейсный заместитель таски для множественного ожидания, предзахватывает ресурс future объектов, чтобы его можно было быстро вернуть в объект при отмене
// *detach - spawn вызов, но для future, с бриджом сигнализации или консьюмером, который поглотит результат future, удобно для мелких операций на которых не хочется останавливаться, работает через запуск рейдера

// Добавить defer stack и insure stack

#endif // ACE_CONTEXT_H
