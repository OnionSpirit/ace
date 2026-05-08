#ifndef UNITS_H
#define UNITS_H

#include <ace/ace.h>

#include <memory>

#include <ace/core/traits/future.h>
#include <ace/core/compose.h>
#include <ace/futures/get_runner.h>
#include <ace/futures/channel.h>
#include <ace/futures/timeout.h>
#include <ace/futures/cutex.h>
#include <ace/futures/network.h>

struct once_suspend : ace::core::traits::busy_future_traits<once_suspend> {

    IMPORT_BUSY_FUTURE_ENV(once_suspend)

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
    ace::console::println("One suspend complete");
    co_return true;
}

inline ace::task nested_context_suspender() {
    co_await simple_context_test();
    ace::console::println("Nested call complete");
    co_return;
}

struct channel_abuser {

    ace::task channel_sender() {
        once_suspend tests_future;
        co_await tests_future;
        std::string msg = "Ping";
        _channel.push(msg);
        co_await ace::console::async::println("Channel send complete");
        const auto received = co_await _channel.pull();
        co_await ace::console::async::println("Channel received answer. DATA: {}", received);
        co_return;
    }

    ace::task channel_receiver() {
        const auto received = co_await _channel.pull();
        co_await ace::console::async::println("Channel receive complete. DATA: {}", received);
        _channel << "Pong";
        co_await ace::console::async::println("Channel send answer");
        co_return;
    }


    ace::futures::channel_dyn<std::string> _channel {};
};

template<typename Rep, typename Period>
ace::task timer_waiter(std::chrono::duration<Rep, Period> wait_time, ace::futures::channel_dyn<long>& ch) {
    const auto start = ace::core::modules::clock::current_time();
    co_await ace::futures::timeout(wait_time);
    const auto end = ace::core::modules::clock::current_time();
    ch << (end - start).count();
    co_return;
}

template<typename Rep, typename Period>
ace::task timer_waiter_valued(std::chrono::duration<Rep, Period> wait_time, ace::futures::channel_dyn<int>& ch) {
    co_await ace::console::async::println("Timeout launched for: {}", wait_time);
    co_await ace::futures::timeout(wait_time);
    co_await ace::console::async::println("Timeout released after: {}", wait_time);
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
    co_await ace::console::async::println("Expires at: {}", fancy(wait_time));
    co_await ace::futures::expire(wait_time);
    co_await ace::console::async::println("Expired at: {}", fancy(wait_time));
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
    auto curr_runner = co_await ace::get_runner();
    co_await ace::futures::timeout(100ms);
    co_await ace::console::async::println("'spawned' runned out");
    output << curr_runner;
    co_return;
}

inline ace::task spawner(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::get_runner();
    output << curr_runner;
    const auto handle = co_await ace::spawn(to_spawn(output));
    while (not handle.done()) {
        co_await ace::console::async::println("'spawned' not done");
        co_await ace::futures::timeout(10ms);
    }
    co_await ace::console::async::println("'spawned' done!!!");
}

inline ace::task join_spawner(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::get_runner();
    output << curr_runner;
    auto handle = co_await ace::spawn(to_spawn(output));
    co_await ace::console::async::println("'spawned' is spawned");
    if (co_await handle.join()) ace::console::println("'spawned' done!!!");
    else co_await ace::console::async::println("'spawned' broken!!!");
}

struct lifetime_watchdog {

    std::string _name;

    explicit lifetime_watchdog(const std::string_view name) : _name(name) {
        ace::console::println("{} constructed", _name);
    };

    ~lifetime_watchdog() { ace::console::println("{} destroyed", _name); }
};

inline ace::promise<> to_spawn_nested(ace::futures::channel_dyn<ace::core::runner*>& output) {
    const auto _check = std::make_unique<lifetime_watchdog>("'parallel-nested'");
    co_await ace::console::async::print("'parallel-nested' started\n");
    co_await ace::futures::timeout(1000ms);
    output << co_await ace::get_runner();
    co_await ace::console::async::println("{} finished", _check->_name);
    co_return;
}

inline ace::task to_spawn_cancel(ace::futures::channel_dyn<ace::core::runner*>& output) {
    const auto _check = std::make_unique<lifetime_watchdog>("'parallel'");
    co_await ace::console::async::print("'parallel' started\n");
    co_await to_spawn_nested(output);
    co_await ace::futures::timeout(1000ms);
    output << co_await ace::get_runner();
    co_await ace::console::async::println("{} finished", _check->_name);
    co_return;
}

inline ace::task spawner_cancel(ace::futures::channel_dyn<ace::core::runner*>& output) {
    co_await ace::console::async::println("'spawner' started");
    auto handle = co_await ace::spawn(to_spawn_cancel(output));
    co_await ace::futures::timeout(100ms);
    co_await ace::console::async::println("'spawner' awake, canceling...");
    handle.cancel();
    output << co_await ace::get_runner();
    co_await ace::console::async::println("'spawner' finished");
}

inline ace::task spawner_join_canceled(ace::futures::channel_dyn<ace::core::runner*>& output) {
    co_await ace::console::async::println("'spawner' started");
    auto handle = co_await ace::spawn(to_spawn_cancel(output));
    co_await ace::futures::timeout(100ms);
    co_await ace::console::async::println("'spawner' awake, canceling...");
    handle.cancel();
    if (not co_await handle.join())
        co_await ace::console::async::println("'parallel' canceled. Joining is 'false'");
    else co_await ace::console::async::println("'parallel' joined as alive. Failure");
    output << co_await ace::get_runner();
    co_await ace::console::async::println("'spawner' finished");
}

inline ace::task racer(const int& max, std::string& shared_counter, ace::cutex& cut) {
    ace::guard crx(cut);
    for (volatile int i = 0; i < max; i = i + 1) {
        co_await crx.capture();
        shared_counter = std::to_string(std::stoi(shared_counter) + 1);
        crx.sync();
    }
    co_await crx.capture();
    co_await ace::console::async::println("'racer' finished");
}

template<typename Rep, typename Period>
ace::task sleeper(std::chrono::duration<Rep, Period> wait_time) {
    co_await ace::futures::timeout(wait_time);
    co_return;
}


inline ace::task cutex_parallel(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    co_await ace::console::async::println("'cutex_parallel' started");
    const auto _check = std::make_unique<lifetime_watchdog>("'cutex_parallel'");
    ace::guard crx(cut);
    co_await crx.capture();
    co_await ace::futures::timeout(50ms);
    output << co_await ace::get_runner();
    co_await ace::console::async::println("{} finished", _check->_name);
}

inline ace::task cutex_carry(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    co_await ace::console::async::println("'cutex_carry' started");
    const auto _check = std::make_unique<lifetime_watchdog>("'cutex_carry'");
    ace::guard crx(cut);
    co_await crx.capture();
    co_await ace::console::async::println("'cutex_carry' captured cutex");
    co_await ace::futures::timeout(100ms);
    output << co_await ace::get_runner();
    co_await ace::console::async::println("{} finished", _check->_name);
}

inline ace::task cutex_checker(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    ace::guard crx(cut);
    co_await crx.capture();
    co_await ace::console::async::println("'cutex_checker' captured cutex");
    output << co_await ace::get_runner();
    co_await ace::console::async::println("'cutex_checker' finished");
}

inline ace::task cutex_spawner(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    co_await ace::console::async::println("'cutex_spawner' started");
    co_await ace::futures::timeout(10ms);
    auto handle = co_await ace::spawn(cutex_carry(output, cut));
    co_await ace::futures::timeout(75ms);
    co_await ace::console::async::println("'cutex_spawner' awake, canceling...");
    handle.cancel();
    co_await ace::futures::timeout(10ms);
    if (not co_await handle.join())
        co_await ace::console::async::println("'cutex_carry' canceled. Joining is 'false'");
    else
        co_await ace::console::async::println("'cutex_carry' joined as alive. Failure");
    output << co_await ace::get_runner();
    co_await ace::console::async::println("'cutex_spawner' finished");
}

inline ace::task cutex_spawner_permanent(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    co_await ace::console::async::println("'cutex_spawner_permanent' started");
    co_await ace::futures::timeout(10ms);
    auto handle = co_await ace::spawn(cutex_carry(output, cut));
    co_await ace::futures::timeout(25ms);
    co_await ace::console::async::println("'cutex_spawner_permanent' awake, canceling...");
    handle.cancel();
    co_await ace::futures::timeout(10ms);
    if (not co_await handle.join())
        co_await ace::console::async::println("'cutex_carry' canceled. Joining is 'false'");
    else
        co_await ace::console::async::println("'cutex_carry' joined as alive. Failure");
    output << co_await ace::get_runner();;
    co_await ace::console::async::println("'cutex_spawner_permanent' finished");
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
        if (co_await connection.send(msg))
            co_await ace::console::async::println("Client sent: '{}'", msg);
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

    for (int i =0; i < 5; ++i) {
        if (auto result = co_await connection.recv_str())
            co_await ace::console::async::println("Server received: '{}'", result.value());
        else co_await ace::console::async::println("Server failed: '{}'", strerror(result.error()));
    }

    co_return;
}


inline ace::task tcp_echo_client() {

    auto bind_entry = co_await ace::futures::io_socket_tcp();
    if (not bind_entry) {
        co_await ace::console::async::println("[ CLIENT ERROR ] - {}", bind_entry.error());
        co_return;
    }

    co_await ace::console::async::println("[ CLIENT ] - Socket created...");

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 8001);
    if (not selection_entry) {
        co_await ace::console::async::println("[ CLIENT ERROR ] - {}", selection_entry.error());
        co_return;
    }

    co_await ace::console::async::println("[ CLIENT ] - Socket bint...");

    const auto connection = co_await selection_entry.connect("127.0.0.1", 8000);
    if (not connection) {
        co_await ace::console::async::println("[ CLIENT ERROR ] - {}", connection.error());
        co_return;
    }

    co_await ace::console::async::println("[ CLIENT ] - Connected to server...");

    for (int i =1; i < 6; ++i) {
        if (const auto input = co_await ace::console::async::input(); not input)
            co_await ace::console::async::println("[ CLIENT ERROR ] : {}", strerror(input.error()));
        else if (co_await connection.send(input.value()))
            co_await ace::console::async::println("[ CLIENT SENT ] : {}", input.value());
    }

    co_return;
}

inline ace::task tcp_echo_server() {

    auto bind_entry = co_await ace::futures::io_socket_tcp();
    if (not bind_entry) {
        co_await ace::console::async::println("[ SERVER ERROR ] - {}", bind_entry.error());
        co_return;
    }

    co_await ace::console::async::println("[ SERVER ] - Socket created...");

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 8000);
    if (not selection_entry) {
        co_await ace::console::async::println("[ SERVER ERROR ] - {}", selection_entry.error());
        co_return;
    }

    co_await ace::console::async::println("[ SERVER ] - Socket bint...");

    auto listener = co_await selection_entry.listen();
    if (not listener) {
        co_await ace::console::async::println("[ SERVER ERROR ] - {}", listener.error());
        co_return;
    }

    co_await ace::console::async::println("[ SERVER ] - Pending connections...");

    const auto connection = co_await listener.accept("127.0.0.1", 8001);
    if (not connection) {
        co_await ace::console::async::println("[ SERVER ERROR ] - {}", connection.error());
        co_return;
    }

    co_await ace::console::async::println("[ SERVER ] - Client connected...");

    std::array<char, 128> buff {};
    for (int i =0; i < 5; ++i) {
        if (co_await connection.recv(buff) > 0)
            co_await ace::console::async::println("[ SERVER RECEIVED ] : {}", buff.data());
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

inline ace::task spawn_post(int idx, ace::futures::channel_dyn<int>& ch) {
    co_await ace::console::async::println("Placing {} to channel", idx);
    ch << idx;
    co_return;
}

inline ace::task imposter(ace::futures::channel_dyn<int>& ch) {
    // NOTE: Spawns parallel and joins all
    auto res = co_await (
                (co_await ace::spawn(spawn_post(1, ch))).join()
            and (co_await ace::post (spawn_post(3, ch))).join()
            and (co_await ace::spawn(spawn_post(2, ch))).join()
            and (co_await ace::post (spawn_post(4, ch))).join()
    );
    // NOTE: Checking composition
    static_assert(std::same_as<decltype(res), std::tuple<bool, bool, bool, bool>>, "Must be tuple of bools");

    // NOTE: Testing syntax depending on stdlibc++ version (newone has more formatters)
    #if defined(__clang__) && __clang_major__ >= 22
        co_await ace::console::async::println("spawn, post, spawn, post - finished {}", res);
    #endif

    co_await ace::console::async::println("Placing {} to channel", 5);
    ch << 5;
    co_return;
}

inline ace::promise<int> pusher(int idx, ace::futures::channel_dyn<int>& ch) {
    ch << idx;
    co_return idx;
}

inline ace::promise<> printer(int idx) {
    co_await ace::console::async::println("Placing {} to channel", idx);
}

inline ace::task graph_starter(ace::futures::channel_dyn<int>& ch) {
    // NOTE: Starting parallel pipes
    co_await (
        pusher(1, ch) | printer
                                         and
        pusher(2, ch) | printer
                                         and
        pusher(3, ch) | printer
                                         and
        pusher(4, ch) | printer
                                         and
        pusher(5, ch) | printer
    );
}

#endif // UNITS_H
