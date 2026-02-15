#ifndef UNITS_H
#define UNITS_H

#include <ace/ace.h>

#include "ace/commands/get_runner.h"
#include "ace/commands/spawn.h"
#include "ace/futures/future.h"
#include "ace/futures/channel.h"
#include "ace/futures/timer.h"
#include "ace/futures/cutex.h"

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

inline ace::async<> nested_context_suspender() {
    co_await simple_context_test();
    std::cout << "Nested call complete" << std::endl;
    co_return;
}

struct channel_abuser {

    ace::async<> channel_sender() {
        once_suspend tests_future;
        co_await tests_future;
        _channel.push(5);
        std::cout << "Channel send complete" << std::endl;
        co_return;
    }

    ace::async<> channel_receiver() {
        auto received = co_await _channel.pull();
        std::cout << "Channel receive complete. DATA: " << received << std::endl;
        co_return;
    }


    ace::futures::channel_dyn<int> _channel {};
};

template<typename Rep, typename Period>
ace::async<> timer_waiter(std::chrono::duration<Rep, Period> wait_time, ace::futures::channel_dyn<long>& ch) {
    const auto start = ace::core::clock::current_time();
    co_await ace::futures::timer(wait_time);
    const auto end = ace::core::clock::current_time();
    ch << (end - start).count();
    co_return;
}

template<typename Rep, typename Period>
ace::async<> timer_waiter_valued(std::chrono::duration<Rep, Period> wait_time, ace::futures::channel_dyn<int>& ch) {
    std::cout << "Timer launched for: " << wait_time << std::endl;
    co_await ace::futures::timer(wait_time);
    std::cout << "Timer released after: " << wait_time << std::endl;
    ch << wait_time.count();
    co_return;
}


inline ace::async<> expire_waiter_valued(ace::core::timepoint_t wait_time, ace::futures::channel_dyn<ace::core::timepoint_t>& ch) {
    std::cout << "Expires at: " << wait_time << std::endl;
    co_await ace::futures::expire(wait_time);
    std::cout << "Expired at: " << wait_time << std::endl;
    ch << wait_time;
    co_return;
}

template <typename channel_t>
ace::async<> channel_fetcher(ace::futures::channel_dyn<channel_t>& ch, std::vector<channel_t>& output) {
    std::vector<channel_t> res {};
    while (not ch.empty()) { res.emplace_back(co_await ch.pull()); }
    output = res;
    co_return;
}

inline ace::async<> to_spawn(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::commands::get_runner();
    co_await ace::futures::timer(100ms);
    std::cout << "'spawned' runned out\n";
    output << curr_runner;
    co_return;
}

inline ace::async<> spawner(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::commands::get_runner();
    output << curr_runner;
    // TODO: Temp
    const ace::coroutines::control_block_handle handle = co_await ace::spawn(to_spawn(output));
    while (not handle.done()) {
        std::cout << "'spawned' not done\n";
        co_await ace::futures::timer(10ms);
    }
    std::cout << "'spawned' done!!!\n";
}

inline ace::async<> to_spawn_cancel(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::commands::get_runner();
    co_await ace::futures::timer(10ms);
    std::cout << "Not canceled\n";
    output << curr_runner;
    co_return;
}

inline ace::async<> spawner_cancel(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::commands::get_runner();
    output << curr_runner;
    // TODO: Temp
    const ace::coroutines::control_block_handle handle = co_await ace::spawn(to_spawn_cancel(output));
    handle.cancel();
    co_await ace::futures::timer(100ms);
    if (handle.done())
        std::cout << "Cancel done!!!\n";
}

inline ace::async<> racer(const int& max, int& shared_counter, ace::cutex& cutx,
        ace::futures::channel_dyn<char>& racer_output) {
    for (volatile int i = 0; i < max; i = i + 1) {
        co_await cutx.capture();
        shared_counter = shared_counter + 1;
        cutx.sync();
    }
    racer_output << 1;
    std::cout << "'racer' finished\n";
}

inline ace::async<> secure_racer(const int& max, int& shared_counter, ace::cutex& cutx,
        ace::futures::channel_dyn<char>& racer_output) {
    for (volatile int i = 0; i < max; i = i + 1) {
        auto sec = ace::secure_capture(cutx);
        co_await sec.capture();
        shared_counter = shared_counter + 1;
    }
    racer_output << 1;
    std::cout << "'racer' finished\n";
}

// NOTE: Helper function for tests
inline ace::async<> cutex_detacher(ace::cutex& cutx, const int racers_count,
        ace::futures::channel_dyn<char>& racer_output) {
    for (int i = 0; i < racers_count; ++i)
        co_await racer_output.pull();
    ace::terminate();
    // ace::core::fixer::detach_cutex(&cutx);
}
#endif // UNITS_H
