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

// ToDo: yield операцию преретащить в генератор
namespace ace::async {
    template<typename returnT =void, is_promise_rule launch_ruleT =differed>
    struct context : future_traits<context<returnT> > {
        DECLARE_FUTURE(context)
        IMPORT_FUTURE_ENV

        struct promise_type;

        typedef std::coroutine_handle<promise_type> coroutine_t;

        coroutine_t _coroutine;

        context() = default;

        // NOTE: Добавить создание id если таковой будет
        explicit context(coroutine_t &&h) : _coroutine{h} {
        };

        operator bool() const { return _coroutine.done(); }

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

        // NOTE: Добавить удаление id если таковой будет
        ~context() override {
            /* while (not _resolved) this->resolve(); */
        };

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
                this->interrupt("Unhandled exception.");
            }

            void interrupt(const std::string_view &&str) {
                std::cerr << "coroutine: " << std::to_string(id)
                        << " - interrupted by: " << str
                        << " Destroying context..." << std::endl;
                this->final_suspend();
                _retcode = -1;
            };

            auto get_return_object() noexcept { return context{coroutine_t::from_promise(*this)}; }

            static auto get_return_object_on_allocation_failure() { return context(nullptr); }
        };

        bool await_ready() override {
            // NOTE: Если корутина выполнена или реакция резрешена, возвращаем true без дальнейших проверок
            if (_coroutine.done()) return true;
            if (_coroutine.promise()._future) {
                if (_coroutine.promise()._future.get()->await_ready()) {
                    _coroutine.promise()._future.release();
                    _coroutine.promise()._future.reset();
                    _coroutine.resume();
                }
            } else _coroutine.resume();
            return _coroutine.done();
        }

        template<typename promiseT>
        bool await_suspend(std::coroutine_handle<promiseT>) {
            if (_coroutine.promise()._retcode
                or _coroutine.done()
                or _coroutine.promise()._status == e_executed_with_value) {
                return false;
            } else [[likely]] { return true; }
        }

        returnT await_resume() {
            if constexpr (requires(promise_type promise_t) { promise_t._return_value; }) {
                return _coroutine.promise()._return_value;
            }
        }

        returnT awake(promise_touch_result *const _res = nullptr) noexcept {
            if (_coroutine) [[likely]] {
                // std::cout << "RESUMING ID: " << _coroutine.promise().id << std::endl;

                _coroutine.promise()._status = e_blocked;

                if (not _coroutine.promise()._future) {
                    if constexpr (std::same_as<void, returnT>) {
                        _coroutine();
                        _coroutine.promise()._status = e_executed;
                    } else {
                        _coroutine();
                        if (_coroutine.promise()._status not_eq e_executed_with_value) {
                            _coroutine.promise()._status = e_executed;
                        }
                    }
                } else if (_coroutine.promise()._future->await_ready()) {
                    _coroutine.promise()._future.reset();
                    _coroutine();
                    _coroutine.promise()._status = e_executed;
                } else { _coroutine.promise()._status = e_blocked; }

                if (_res != nullptr) {
                    *_res = _coroutine.promise()._status;
                }

                if constexpr (not std::same_as<void, returnT>) return returnT{};
            }

            if constexpr (not std::same_as<void, returnT>) {
                return this->_coroutine.promise()._return_value;
            }
        }
    };
}

// Говно нахуй не нужное, но
// bridge - обобщенный канал для отслеживания состояний запущеных в параллель асинхронных компонент, и отправки управляющих сигналов
// raider - интерфейсный заместитель таски для множественного ожидания, предзахватывает ресурс future объектов, чтобы его можно было быстро вернуть в объект при отмене
// *detach - spawn вызов, но для future, с бриджом сигнализации или консьюмером, который поглотит результат future, удобно для мелких операций на которых не хочется останавливаться, работает через запуск рейдера

// Добавить defer stack и insure stack

#endif // ACE_CONTEXT_H
