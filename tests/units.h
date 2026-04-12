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
#include "ace/futures/network.h"
#include "ace/core/composed.h"

struct once_suspend : ace::futures::busy_future_traits<once_suspend> {

    IMPORT_BUSY_FUTURE_ENV(once_suspend)

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

struct channel_abuser {

    ace::task channel_sender() {
        once_suspend tests_future;
        co_await tests_future;
        std::string msg = "Ping";
        _channel.push(msg);
        std::cout << "Channel send complete" << std::endl;
        const auto received = co_await _channel.pull();
        std::cout << "Channel received answer. DATA: " << received << std::endl;
        co_return;
    }

    ace::task channel_receiver() {
        const auto received = co_await _channel.pull();
        std::cout << "Channel receive complete. DATA: " << received << std::endl;
        _channel << "pong";
        std::cout << "Channel send answer" << std::endl;
        co_return;
    }


    ace::futures::channel_dyn<std::string> _channel {};
};

template<typename Rep, typename Period>
ace::task timer_waiter(std::chrono::duration<Rep, Period> wait_time, ace::futures::channel_dyn<long>& ch) {
    const auto start = ace::core::clock::current_time();
    co_await ace::futures::timeout(wait_time);
    const auto end = ace::core::clock::current_time();
    ch << (end - start).count();
    co_return;
}

template<typename Rep, typename Period>
ace::task timer_waiter_valued(std::chrono::duration<Rep, Period> wait_time, ace::futures::channel_dyn<int>& ch) {
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

inline ace::task expire_waiter_valued(ace::core::timepoint_t wait_time, ace::futures::channel_dyn<ace::core::timepoint_t>& ch) {
    std::cout << "Expires at: " << fancy(wait_time) << std::endl;
    co_await ace::futures::expire(wait_time);
    std::cout << "Expired at: " << fancy(wait_time) << std::endl;
    ch << wait_time;
    co_return;
}

template <typename channel_t>
ace::task channel_fetcher(ace::futures::channel_dyn<channel_t>& ch, std::vector<channel_t>& output) {
    std::vector<channel_t> res {};
    while (not ch.empty()) { res.emplace_back(co_await ch.pull()); }
    output = res;
    co_return;
}

inline ace::task to_spawn(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::commands::get_runner();
    co_await ace::futures::timeout(100ms);
    std::cout << "'spawned' runned out\n";
    output << curr_runner;
    co_return;
}

inline ace::task spawner(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::commands::get_runner();
    output << curr_runner;
    const auto handle = co_await ace::spawn(to_spawn(output));
    while (not handle.done()) {
        std::cout << "'spawned' not done\n";
        co_await ace::futures::timeout(10ms);
    }
    std::cout << "'spawned' done!!!\n";
}

inline ace::task join_spawner(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::commands::get_runner();
    output << curr_runner;
    auto handle = co_await ace::spawn(to_spawn(output));
    std::cout << "'spawned' is spawned\n";
    if (co_await handle.join()) std::cout << "'spawned' done!!!\n";
    else std::cout << "'spawned' broken!!!\n";
}

struct lifetime_watchdog {

    std::string _name;

    explicit lifetime_watchdog(const std::string_view name) : _name(name) {
        std::cout << _name << " constructed" << std::endl;
    };

    ~lifetime_watchdog() { std::cout << _name << " destroyed" << std::endl; }
};

inline ace::promise<> to_spawn_nested(ace::futures::channel_dyn<ace::core::runner*>& output) {
    std::cout << "'parallel-nested' started\n";
    co_await ace::futures::timeout(10ms);
    co_await ace::suspend();
    const auto _check = std::make_unique<lifetime_watchdog>("'parallel-nested'");
    co_await ace::futures::timeout(1000ms);
    output << co_await ace::commands::get_runner();
    std::cout << _check->_name << " finished\n";
    co_return;
}

inline ace::task to_spawn_cancel(ace::futures::channel_dyn<ace::core::runner*>& output) {
    std::cout << "'parallel' started\n";
    co_await ace::futures::timeout(10ms);
    const auto _check = std::make_unique<lifetime_watchdog>("'parallel'");
    co_await to_spawn_nested(output);
    co_await ace::futures::timeout(1000ms);
    output << co_await ace::commands::get_runner();
    std::cout << _check->_name << " finished\n";
    co_return;
}

inline ace::task spawner_cancel(ace::futures::channel_dyn<ace::core::runner*>& output) {
    std::cout << "'spawner' started\n";
    auto handle = co_await ace::spawn(to_spawn_cancel(output));
    co_await ace::futures::timeout(100ms);
    std::cout << "'spawner' awake, canceling...\n";
    handle.cancel();
    output << co_await ace::commands::get_runner();
    co_await ace::futures::timeout(10ms);
    std::cout << "'spawner' finished\n";
}

inline ace::task spawner_join_canceled(ace::futures::channel_dyn<ace::core::runner*>& output) {
    std::cout << "'spawner' started\n";
    auto handle = co_await ace::spawn(to_spawn_cancel(output));
    co_await ace::futures::timeout(100ms);
    std::cout << "'spawner' awake, canceling...\n";
    handle.cancel();
    co_await ace::futures::timeout(10ms);
    if (not co_await handle.join())
        std::cout << "'parallel' canceled. Joining is 'false'\n";
    else std::cout << "'parallel' joined as alive. Failure\n";
    output << co_await ace::commands::get_runner();
    std::cout << "'spawner' finished\n";
}

inline ace::task racer(const int& max, std::string& shared_counter, ace::cutex& cut) {
    ace::guard crx(cut);
    for (volatile int i = 0; i < max; i = i + 1) {
        co_await crx.capture();
        shared_counter = std::to_string(std::stoi(shared_counter) + 1);
        crx.sync();
    }
    co_await crx.capture();
    std::cout << "'racer' finished\n";
}

template<typename Rep, typename Period>
ace::task sleeper(std::chrono::duration<Rep, Period> wait_time) {
    co_await ace::futures::timeout(wait_time);
    co_return;
}


inline ace::task cutex_parallel(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    std::cout << "'cutex_parallel' started\n";
    const auto _check = std::make_unique<lifetime_watchdog>("'cutex_parallel'");
    ace::guard crx(cut);
    co_await crx.capture();
    co_await ace::futures::timeout(50ms);
    output << co_await ace::commands::get_runner();
    std::cout << _check->_name << " finished\n";
}

inline ace::task cutex_carry(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    std::cout << "'cutex_carry' started\n";
    const auto _check = std::make_unique<lifetime_watchdog>("'cutex_carry'");
    ace::guard crx(cut);
    co_await crx.capture();
    std::cout << "'cutex_carry' captured cutex\n";
    co_await ace::futures::timeout(100ms);
    output << co_await ace::commands::get_runner();
    std::cout << _check->_name << " finished\n";
}

inline ace::task cutex_checker(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    ace::guard crx(cut);
    co_await crx.capture();
    std::cout << "'cutex_checker' captured cutex\n";
    output << co_await ace::commands::get_runner();;
    std::cout << "'cutex_checker' finished\n";
}

inline ace::task cutex_spawner(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    std::cout << "'cutex_spawner' started\n";
    co_await ace::futures::timeout(10ms);
    auto handle = co_await ace::spawn(cutex_carry(output, cut));
    co_await ace::futures::timeout(75ms);
    std::cout << "'cutex_spawner' awake, canceling...\n";
    handle.cancel();
    co_await ace::futures::timeout(10ms);
    if (not co_await handle.join())
        std::cout << "'cutex_carry' canceled. Joining is 'false'\n";
    else
        std::cout << "'cutex_carry' joined as alive. Failure\n";
    output << co_await ace::commands::get_runner();;
    std::cout << "'cutex_spawner' finished\n";
}

inline ace::task cutex_spawner_permanent(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    std::cout << "'cutex_spawner_permanent' started\n";
    co_await ace::futures::timeout(10ms);
    auto handle = co_await ace::spawn(cutex_carry(output, cut));
    co_await ace::futures::timeout(25ms);
    std::cout << "'cutex_spawner_permanent' awake, canceling...\n";
    handle.cancel();
    co_await ace::futures::timeout(10ms);
    if (not co_await handle.join())
        std::cout << "'cutex_carry' canceled. Joining is 'false'\n";
    else
        std::cout << "'cutex_carry' joined as alive. Failure\n";
    output << co_await ace::commands::get_runner();;
    std::cout << "'cutex_spawner_permanent' finished\n";
}

inline ace::task socket_abuser() {

    auto bind_entry = co_await ace::futures::io_socket_tcp();
    if (not bind_entry) {
        std::cerr << bind_entry.error() << std::endl;
        co_return;
    }

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 8001);
    if (not selection_entry) {
        std::cerr << selection_entry.error() << std::endl;
        co_return;
    }

    const auto connection = co_await selection_entry.connect("127.0.0.1", 8000);
    if (not connection) {
        std::cerr << connection.error() << std::endl;
        co_return;
    }

    for (int i =1; i < 6; ++i) {
        std::string msg = "Echo message " + std::to_string(i);
        if (co_await connection.send(msg.c_str(), msg.size()) == msg.size())
            std::cout << "Client sent: '" << msg << "'\n";
    }

    co_return;
}

inline ace::task socket_listener() {

    auto bind_entry = co_await ace::futures::io_socket_tcp();
    if (not bind_entry) {
        std::cerr << bind_entry.error() << std::endl;
        co_return;
    }

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 8000);
    if (not selection_entry) {
        std::cerr << selection_entry.error() << std::endl;
        co_return;
    }

    auto listener = co_await selection_entry.listen();
    if (not listener) {
        std::cerr << listener.error() << std::endl;
        co_return;
    }

    const auto connection = co_await listener.accept("127.0.0.1", 8001);
    if (not connection) {
        std::cerr << connection.error() << std::endl;
        co_return;
    }

    char buff[128];
    for (int i =0; i < 5; ++i) {
        memset(buff, 0, 128);
        if (co_await connection.recv(buff, 128) > 0)
            std::cout << "Server received: '" << buff << "'\n";
    }

    co_return;
}


inline ace::task input_echo() {

    static constexpr int READ_BUFF_LEN = 128;
    char buff[READ_BUFF_LEN];
    auto msg = "[ TEST MESSAGE ]";

    for (int i =0; i < 5; ++i) {
        memset(buff, 0, 128);
        if (co_await ace::core::read_query(STDIN_FILENO, buff, READ_BUFF_LEN) > 0) {
            std::string send_buff = "[ ECHO ] : " + std::string(buff) + "\n";
            if (co_await ace::core::write_query(STDIN_FILENO, send_buff.c_str(), send_buff.size()) < 1)
                co_return;
        }
    }

    co_return;
}


inline ace::task tcp_echo_client() {

    auto bind_entry = co_await ace::futures::io_socket_tcp();
    if (not bind_entry) {
        std::cout << "[ CLIENT ERROR ] - " << bind_entry.error() << std::endl;
        co_return;
    }

    std::cout << "[ CLIENT ] - Socket created...\n";

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 8001);
    if (not selection_entry) {
        std::cout << "[ CLIENT ERROR ] - " << selection_entry.error() << std::endl;
        co_return;
    }

    std::cout << "[ CLIENT ] - Socket bint...\n";

    const auto connection = co_await selection_entry.connect("127.0.0.1", 8000);
    if (not connection) {
        std::cout << "[ CLIENT ERROR ] - " << connection.error() << std::endl;
        co_return;
    }

    std::cout << "[ CLIENT ] - Connected to server...\n";

    static constexpr int READ_BUFF_LEN = 128;

    char buff[READ_BUFF_LEN];

    for (int i =1; i < 6; ++i) {
        memset(buff, 0, READ_BUFF_LEN);
        const bool is_sent = co_await ace::core::read_query(STDIN_FILENO, buff, READ_BUFF_LEN)
            and co_await connection.send(buff, READ_BUFF_LEN);

        if (is_sent) {
            std::string msg = "[ CLIENT SENT ] : " + std::string(buff);
            if (co_await ace::core::write_query(STDIN_FILENO, msg.c_str(), msg.size()) < 1)
                co_return;
        }
    }

    co_return;
}

inline ace::task tcp_echo_server() {

    auto bind_entry = co_await ace::futures::io_socket_tcp();
    if (not bind_entry) {
        std::cout << "[ SERVER ERROR ] - " << bind_entry.error() << std::endl;
        co_return;
    }

    std::cout << "[ SERVER ] - Socket created...\n";

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 8000);
    if (not selection_entry) {
        std::cout << "[ SERVER ERROR ] - " << selection_entry.error() << std::endl;
        co_return;
    }

    std::cout << "[ SERVER ] - Socket bint...\n";

    auto listener = co_await selection_entry.listen();
    if (not listener) {
        std::cout << "[ SERVER ERROR ] - " << listener.error() << std::endl;
        co_return;
    }

    std::cout << "[ SERVER ] - Pending connections...\n";

    const auto connection = co_await listener.accept("127.0.0.1", 8001);
    if (not connection) {
        std::cout << "[ SERVER ERROR ] - " << connection.error() << std::endl;
        co_return;
    }

    // bind_entry.extract()
    std::cout << "[ SERVER ] - Client connected...\n";

    static constexpr int READ_BUFF_LEN = 128;

    char buff[READ_BUFF_LEN];
    for (int i =0; i < 5; ++i) {
        memset(buff, 0, READ_BUFF_LEN);
        if (co_await connection.recv(buff, READ_BUFF_LEN) > 0)
            std::cout << "[ SERVER RECEIVED ] : " << std::string(buff);
    }

    co_return;
}

inline ace::task timer_or_timer() {
    auto long_timeout = ace::futures::timeout(10s);
    co_await (long_timeout or ace::futures::timeout(100ms));
    co_return;
}

inline ace::task timer_and_timer() {
    auto long_timeout = ace::futures::timeout(100ms);
    co_await (long_timeout and ace::futures::timeout(10ms));
    co_return;
}

#endif // UNITS_H
