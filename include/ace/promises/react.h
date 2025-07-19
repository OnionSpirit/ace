/**
 * @file
 * @details This file contains a future_handler, future_trait classes and its
 * dispatching concepts: is_future_accept_promise, is_future_accept_coroutine,
 * is_future. Types are intended to be used to create derived future objects,
 * that will be processed by co_await operator
 */
#ifndef ACE_REACT_H
#define ACE_REACT_H

#include "ace/futures/future.h"
#include "ace/promises/promise.h"
#include <coroutine>

namespace ace::async {


    // todo: избавиться от этого недоразумения
    template<typename returnT =void>
    struct react : future_traits<react<returnT>> {

        DECLARE_FUTURE(react)
        IMPORT_FUTURE_ENV

        struct promise_type;

        typedef std::coroutine_handle<promise_type> coroutine_t;

        coroutine_t _coroutine;

        bool _resolved { true };

        react() = default;

        // NOTE: Когда задаём корутину в реакцию, она считается не разрешенной и её надо разрешать
        explicit react(coroutine_t&& h) : _coroutine{h} { _resolved = false; };

        // NOTE: Возвращает текущий результат разрешения
        operator bool() const { return _resolved; }

        // NOTE: Пытается разрешить реакцию при проверке, и вернуть результат разрешения
        operator bool() { return this->resolve(); }

        // react(const react &) =delete;

        // react &operator=(const react &) =delete;

        // react(react&& t) noexcept  {
        //     _coroutine = t._coroutine;
        //     t._coroutine = nullptr;
        //     _coroutine._unresolved = t._unresolved;
        // }

        // react &operator=(react&& t) noexcept {
        //     if (_coroutine) [[likely]] _coroutine.destroy();
        //     _coroutine = t._coroutine;
        //     t._coroutine = nullptr;
        //     _coroutine._unresolved = t._unresolved;
        //     return *this;
        // }

        // NOTE: Будем пытаться полностью разрешить реакцию если она не является разрешенной
        ~react() override { while (not _resolved) this->resolve(); };

        struct promise_type : public promise_traits<returnT> {

            DECLARE_PROMISE_TRAITS(returnT)
            IMPORT_PROMISE_TRAITS_ENV

            promise_type() =default;

            ~promise_type() =default;

            auto initial_suspend() const noexcept { return std::suspend_always{}; }

            auto final_suspend() noexcept { return std::suspend_always{}; }

            void unhandled_exception() {
                this->interrupt("Unhandled exception.");
            }

            void interrupt(const std::string_view&& str) {
                std::cerr << "coroutine: " << std::to_string(id)
                          << " - interrupted by: " << str
                          <<  " Destroying context..." << std::endl;
                this->final_suspend();
                _retcode = -1;
            };

            auto get_return_object() noexcept { return react { coroutine_t::from_promise(*this) }; }

        };

        virtual bool await_ready() override {
            // NOTE: Если корутина выполнена или реакция резрешена, возвращаем true без дальнейших проверок
            if (_coroutine.done() or _resolved) return true;
            if (_coroutine.promise()._future) {
                if (_coroutine.promise()._future.get()->await_ready()) {
                    _coroutine.promise()._future.release();
                    _coroutine.promise()._future.reset();
                    _coroutine.resume();
                }
            } else _coroutine.resume();
            return _coroutine.done();
        }

        template <typename promiseT>
        bool await_suspend(std::coroutine_handle<promiseT>) {
            if (_coroutine.promise()._retcode
                or _coroutine.done()
                or _coroutine.promise()._status == promise_touch_result::e_executed_with_value) {
                return false;
            } else [[likely]] { return true; }
        }

        returnT await_resume() {
            if constexpr (requires(promise_type promise_t) { promise_t._return_value; } ) {
                return _coroutine.promise()._return_value;
            }
        }

        // NOTE: Помечает реакцию как разрешенную, очищая при этом future
        void release() {
            _coroutine.promise()._future.release();
            _coroutine.promise()._future.reset();
            _resolved = true;
        }

        // NOTE: Проводит попытку разрешения реакции
        bool resolve() {
            if (_resolved) return _resolved;
            else return _resolved = await_ready();
        }

        // void notify() { handle(); }

    };

}

// Говно нахуй не нужное, но
// bridge(trackerreact.h) - обобщенный канал для отслеживания состояний запущеных в параллель асинхронных компонент, нужно чтобы понимать если там всё сломалось. Либо впихнем этот функционал в канал
// raider - интерфейсный заместитель таски для множественного ожидания, предзахватывает ресурс future объектов, чтобы его можно было быстро вернуть в объект при отмене
// *detach - spawn вызов, но для future, с бриджом сигнализации или консьюмером который поглотит результат future, удобно для мелких операций на которых не хочется останавливаться, работает через запуск рейдера
//

#endif // ACE_REACT_H
