#ifndef UNITS_H
#define UNITS_H

#include <ace/ace.h>

#include <memory>

#include "ace/commands/get_runner.h"
#include "ace/commands/spawn.h"
#include "ace/futures/future.h"
#include "ace/futures/channel.h"
#include "ace/futures/timeout.h"
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
    co_await ace::futures::timeout(wait_time);
    const auto end = ace::core::clock::current_time();
    ch << (end - start).count();
    co_return;
}

template<typename Rep, typename Period>
ace::async<> timer_waiter_valued(std::chrono::duration<Rep, Period> wait_time, ace::futures::channel_dyn<int>& ch) {
    std::cout << "Timeout launched for: " << wait_time << std::endl;
    co_await ace::futures::timeout(wait_time);
    std::cout << "Timeout released after: " << wait_time << std::endl;
    ch << wait_time.count();
    co_return;
}

inline auto fancy(ace::core::timepoint_t tp) {
    auto offset =
        std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch()
      - std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()).time_since_epoch();
    return std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>{
        std::chrono::time_point_cast<std::chrono::milliseconds>(tp + offset).time_since_epoch()
    };
}

inline ace::async<> expire_waiter_valued(ace::core::timepoint_t wait_time, ace::futures::channel_dyn<ace::core::timepoint_t>& ch) {
    std::cout << "Expires at: " << fancy(wait_time) << std::endl;
    co_await ace::futures::expire(wait_time);
    std::cout << "Expired at: " << fancy(wait_time) << std::endl;
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
    co_await ace::futures::timeout(100ms);
    std::cout << "'spawned' runned out\n";
    output << curr_runner;
    co_return;
}

inline ace::async<> spawner(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::commands::get_runner();
    output << curr_runner;
    const auto handle = co_await ace::spawn(to_spawn(output));
    while (not handle.done()) {
        std::cout << "'spawned' not done\n";
        co_await ace::futures::timeout(10ms);
    }
    std::cout << "'spawned' done!!!\n";
}

inline ace::async<> join_spawner(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::commands::get_runner();
    output << curr_runner;
    auto handle = co_await ace::spawn(to_spawn(output));
    std::cout << "'spawned' is spawned\n";
    if (co_await handle.join()) std::cout << "'spawned' done!!!\n";
    else std::cout << "'spawned' broken!!!\n";
}

struct destruct_on_cancel_checker {

    std::string _name;

    explicit destruct_on_cancel_checker(const std::string_view name) : _name(name) {
        std::cout << _name << " constructed" << std::endl;
    };

    ~destruct_on_cancel_checker() { std::cout << _name << " destroyed" << std::endl; }
};

inline ace::promise<> to_spawn_nested(ace::futures::channel_dyn<ace::core::runner*>& output) {
    std::cout << "'parallel-nested' started\n";
    co_await ace::futures::timeout(10ms);
    auto curr_runner = co_await ace::commands::get_runner();
    co_await ace::suspend();
    const auto _check = std::make_unique<destruct_on_cancel_checker>("'parallel-nested'");
    co_await ace::futures::timeout(1000ms);
    output << curr_runner;
    std::cout << _check->_name << " finished\n";
    co_return;
}

inline ace::async<> to_spawn_cancel(ace::futures::channel_dyn<ace::core::runner*>& output) {
    std::cout << "'parallel' started\n";
    co_await ace::futures::timeout(10ms);
    auto curr_runner = co_await ace::commands::get_runner();
    const auto _check = std::make_unique<destruct_on_cancel_checker>("'parallel'");
    co_await to_spawn_nested(output);
    co_await ace::futures::timeout(1000ms);
    output << curr_runner;
    std::cout << _check->_name << " finished\n";
    co_return;
}

inline ace::async<> spawner_cancel(ace::futures::channel_dyn<ace::core::runner*>& output) {
    std::cout << "'spawner' started\n";
    auto curr_runner = co_await ace::commands::get_runner();
    // TODO: Temp
    auto handle = co_await ace::spawn(to_spawn_cancel(output));
    co_await ace::futures::timeout(100ms);
    std::cout << "'spawner' awake, canceling...\n";
    handle.cancel();
    output << curr_runner;
    co_await ace::futures::timeout(10ms);
    std::cout << "'spawner' finished\n";
}

inline ace::async<> racer(const int& max, std::string& shared_counter, ace::cutex& cut) {
    for (volatile int i = 0; i < max; i = i + 1) {
        ace::croxy crx(cut);
        co_await crx.capture();
        shared_counter = std::to_string(std::stoi(shared_counter) + 1);
    }
    std::cout << "'racer' finished\n";
}

template<typename Rep, typename Period>
ace::async<> sleeper(std::chrono::duration<Rep, Period> wait_time) {
    co_await ace::futures::timeout(wait_time);
    co_return;
}

#endif // UNITS_H
