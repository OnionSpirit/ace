#ifndef UNITS_H
#define UNITS_H

#include "ace/common/aliases.h"
#include "include/ace/futures/future.h"

struct once_suspend : ace::futures::future_traits<once_suspend> {

    DECLARE_FUTURE(once_suspend)
    IMPORT_FUTURE_ENV

    // once_suspend() { std::cout << "Future constructed" << '\n'; }
    // once_suspend(const once_suspend& t) { std::cout << "once_suspend copy const" << std::endl; }
    // once_suspend(const once_suspend&& t) {
    //     std::cout << "once_suspend move constr" << std::endl;
    // }

    bool _trigger { false };

    bool await_ready() override {
        if (not _trigger) {
            _trigger = true;
            return false;
        } else return true;
    }

    void await_suspend(auto) {};

    auto await_resume() { }

    ~once_suspend() override = default;
};

inline ace::promise<bool> simple_context_test() {
    once_suspend tests_future;

    co_await tests_future;
    std::cout << "One suspend complete" << std::endl;
    co_return true;
}

inline ace::task nested_context_suspender() {
    co_await simple_context_test();
    std::cout << "Nested call complete" << std::endl;
    co_return;
}


#endif // UNITS_H
