#ifndef UNITS_H
#define UNITS_H

#include <memory>
#include <cstring>
#include <ace/ace.h>
#include <ace/core/traits/future.h>
#include <ace/core/compose.h>
#include <ace/futures/get_runner.h>
#include <ace/futures/channel.h>
#include <ace/futures/timeout.h>
#include <ace/futures/cutex.h>
#include <ace/core/tools/lifetime.h>
#include <ace/console.h>
#include <ace/net.h>

namespace tool = ace::core::tools;

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
        ace::console::println("Channel send complete");
        co_await ace::suspend();
        const auto received = co_await _channel.pull();
        ace::console::println("Channel received answer. DATA: {}", received);
        co_return;
    }

    ace::task channel_receiver() {
        const auto received = co_await _channel.pull();
        ace::console::println("Channel receive complete. DATA: {}", received);
        _channel << "Pong";
        ace::console::println("Channel send answer");
        co_return;
    }


    ace::futures::channel_dyn<std::string> _channel {};
};

template<typename Rep, typename Period>
ace::task timer_waiter(std::chrono::duration<Rep, Period> wait_time, ace::futures::channel_dyn<long>& ch) {
    const auto start = ace::core::services::clock::current_time();
    co_await ace::futures::timeout(wait_time);
    const auto end = ace::core::services::clock::current_time();
    ch << (end - start).count();
    co_return;
}

template<typename Rep, typename Period>
ace::task timer_waiter_valued(std::chrono::duration<Rep, Period> wait_time, ace::futures::channel_dyn<int>& ch) {
    ace::console::println("Timeout launched for: {}", wait_time);
    co_await ace::futures::timeout(wait_time);
    ace::console::println("Timeout released after: {}", wait_time);
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
    ace::console::println("Expires at: {}", fancy(wait_time));
    co_await ace::futures::expire(wait_time);
    ace::console::println("Expired at: {}", fancy(wait_time));
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
    ace::console::println("'spawned' runned out");
    output << curr_runner;
    co_return;
}

inline ace::task spawner(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::get_runner();
    output << curr_runner;
    const auto handle = co_await ace::spawn(to_spawn(output));
    while (not handle.done()) {
        ace::console::println("'spawned' not done");
        co_await ace::futures::timeout(10ms);
    }
    ace::console::println("'spawned' done!!!");
}

inline ace::task join_spawner(ace::futures::channel_dyn<ace::core::runner*>& output) {
    auto curr_runner = co_await ace::get_runner();
    output << curr_runner;
    auto handle = co_await ace::spawn(to_spawn(output));
    ace::console::println("'spawned' is spawned");
    if (co_await handle.join()) ace::console::println("'spawned' done!!!");
    else ace::console::println("'spawned' broken!!!");
}

inline ace::promise<> to_spawn_nested(ace::futures::channel_dyn<ace::core::runner*>& output) {
    const auto wd = tool::lifetime("'parallel-nested'");
    ace::console::print("'parallel-nested' started\n");
    co_await ace::futures::timeout(1000ms);
    output << co_await ace::get_runner();
    ace::console::println("{} finished", wd.mark());
    co_return;
}

inline ace::task to_spawn_cancel(ace::futures::channel_dyn<ace::core::runner*>& output) {
    const auto wd = tool::lifetime("'parallel'");
    ace::console::print("'parallel' started\n");
    co_await to_spawn_nested(output);
    co_await ace::futures::timeout(1000ms);
    output << co_await ace::get_runner();
    ace::console::println("{} finished", wd.mark());
    co_return;
}

inline ace::task spawner_cancel(ace::futures::channel_dyn<ace::core::runner*>& output) {
    ace::console::println("'spawner' started");
    auto handle = co_await ace::spawn(to_spawn_cancel(output));
    co_await ace::futures::timeout(100ms);
    ace::console::println("'spawner' awake, canceling...");
    handle.cancel();
    output << co_await ace::get_runner();
    ace::console::println("'spawner' finished");
}

inline ace::task spawner_join_canceled(ace::futures::channel_dyn<ace::core::runner*>& output) {
    ace::console::println("'spawner' started");
    auto handle = co_await ace::spawn(to_spawn_cancel(output));
    co_await ace::futures::timeout(100ms);
    ace::console::println("'spawner' awake, canceling...");
    handle.cancel();
    if (not co_await handle.join())
        ace::console::println("'parallel' canceled. Joining is 'false'");
    else ace::console::println("'parallel' joined as alive. Failure");
    output << co_await ace::get_runner();
    ace::console::println("'spawner' finished");
}

inline ace::task racer(const int& max, std::string& shared_counter, ace::cutex& cut) {
    ace::guard crx(cut);
    for (volatile int i = 0; i < max; i = i + 1) {
        co_await crx.capture();
        shared_counter = std::to_string(std::stoi(shared_counter) + 1);
        crx.sync();
        // NOTE: sync twice to check no-op state
        crx.sync();
    }
    co_await crx.capture();
    ace::console::println("'racer' finished");
}

template<typename Rep, typename Period>
ace::task sleeper(std::chrono::duration<Rep, Period> wait_time) {
    co_await ace::futures::timeout(wait_time);
    co_return;
}


inline ace::task cutex_parallel(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    ace::console::println("'cutex_parallel' started");
    const auto wd = tool::lifetime("'cutex_parallel'");
    ace::guard crx(cut);
    co_await crx.capture();
    co_await ace::futures::timeout(50ms);
    output << co_await ace::get_runner();
    ace::console::println("{} finished", wd.mark());
}

inline ace::task cutex_carry(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    ace::console::println("'cutex_carry' started");
    const auto wd = tool::lifetime("'cutex_carry'");
    ace::guard crx(cut);
    co_await crx.capture();
    ace::console::println("'cutex_carry' captured cutex");
    co_await ace::futures::timeout(100ms);
    output << co_await ace::get_runner();
    ace::console::println("{} finished", wd.mark());
}

inline ace::task cutex_checker(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    ace::guard crx(cut);
    co_await crx.capture();
    ace::console::println("'cutex_checker' captured cutex");
    output << co_await ace::get_runner();
    ace::console::println("'cutex_checker' finished");
}

inline ace::task cutex_spawner(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    ace::console::println("'cutex_spawner' started");
    co_await ace::futures::timeout(10ms);
    auto handle = co_await ace::spawn(cutex_carry(output, cut));
    co_await ace::futures::timeout(75ms);
    ace::console::println("'cutex_spawner' awake, canceling...");
    handle.cancel();
    co_await ace::futures::timeout(10ms);
    if (not co_await handle.join())
        ace::console::println("'cutex_carry' canceled. Joining is 'false'");
    else
        ace::console::println("'cutex_carry' joined as alive. Failure");
    output << co_await ace::get_runner();
    ace::console::println("'cutex_spawner' finished");
}

inline ace::task cutex_spawner_permanent(ace::futures::channel_dyn<ace::core::runner*>& output, ace::cutex& cut) {
    ace::console::println("'cutex_spawner_permanent' started");
    co_await ace::futures::timeout(10ms);
    auto handle = co_await ace::spawn(cutex_carry(output, cut));
    co_await ace::futures::timeout(25ms);
    ace::console::println("'cutex_spawner_permanent' awake, canceling...");
    handle.cancel();
    co_await ace::futures::timeout(10ms);
    if (not co_await handle.join())
        ace::console::println("'cutex_carry' canceled. Joining is 'false'");
    else
        ace::console::println("'cutex_carry' joined as alive. Failure");
    output << co_await ace::get_runner();;
    ace::console::println("'cutex_spawner_permanent' finished");
}

inline ace::task socket_abuser() {

    auto bind_entry = co_await ace::net::io_socket_tcp();
    if (not bind_entry) {
        std::cerr << bind_entry.error() << std::endl;
        ace::terminate();
        co_return;
    }

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 8001);
    if (not selection_entry) {
        std::cerr << selection_entry.error() << std::endl;
        ace::terminate();
        co_return;
    }

    const auto connection = co_await selection_entry.connect("127.0.0.1", 8000);
    if (not connection) {
        std::cerr << connection.error() << std::endl;
        ace::terminate();
        co_return;
    }

    for (int i =1; i < 6; ++i) {
        std::string msg = "Echo message " + std::to_string(i);
        if (co_await connection.send(msg))
            ace::console::println("Client sent: '{}'", msg);
    }

    co_return;
}

inline ace::task socket_listener() {

    auto bind_entry = co_await ace::net::io_socket_tcp();
    if (not bind_entry) {
        std::cerr << bind_entry.error() << std::endl;
        ace::terminate();
        co_return;
    }

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 8000);
    if (not selection_entry) {
        std::cerr << selection_entry.error() << std::endl;
        ace::terminate();
        co_return;
    }

    auto listener = co_await selection_entry.listen();
    if (not listener) {
        std::cerr << listener.error() << std::endl;
        ace::terminate();
        co_return;
    }

    const auto connection = co_await listener.accept("127.0.0.1", 8001);
    if (not connection) {
        std::cerr << connection.error() << std::endl;
        ace::terminate();
        co_return;
    }

    for (int i =0; i < 5; ++i) {
        if (auto result = co_await connection.recv_str())
            ace::console::println("Server received: '{}'", result.value());
        else ace::console::println("Server failed: '{}'", strerror(result.error()));
    }

    co_return;
}


inline ace::task socket_abuser_sg() {

    auto bind_entry = co_await ace::net::io_socket_tcp();
    if (not bind_entry) {
        std::cerr << bind_entry.error() << std::endl;
        ace::terminate();
        co_return;
    }

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 9001);
    if (not selection_entry) {
        std::cerr << selection_entry.error() << std::endl;
        ace::terminate();
        co_return;
    }

    const auto connection = co_await selection_entry.connect("127.0.0.1", 9000);
    if (not connection) {
        std::cerr << connection.error() << std::endl;
        ace::terminate();
        co_return;
    }

    for (int i = 1; i < 6; ++i) {
        std::string msg = "Echo message " + std::to_string(i);
        auto buf = ace::core::services::kernel_controller::iovec_allocate(msg.size());
        if (not buf) {
            ace::terminate();
            co_return;
        }
        std::memcpy(buf->iov.iov_base, msg.data(), msg.size());
        std::array<iovec, 1> iov {{{buf->iov}}};
        if (co_await connection.sendmsg(iov))
            ace::console::println("Client [sg] sent: '{}'", msg);
        ace::core::services::kernel_controller::iovec_deallocate(*buf);
    }

    co_return;
}

inline ace::task socket_listener_sg() {

    auto bind_entry = co_await ace::net::io_socket_tcp();
    if (not bind_entry) {
        std::cerr << bind_entry.error() << std::endl;
        ace::terminate();
        co_return;
    }

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 9000);
    if (not selection_entry) {
        std::cerr << selection_entry.error() << std::endl;
        ace::terminate();
        co_return;
    }

    auto listener = co_await selection_entry.listen();
    if (not listener) {
        std::cerr << listener.error() << std::endl;
        ace::terminate();
        co_return;
    }

    const auto connection = co_await listener.accept("127.0.0.1", 9001);
    if (not connection) {
        std::cerr << connection.error() << std::endl;
        ace::terminate();
        co_return;
    }

    for (int i = 0; i < 5; ++i) {
        auto buf = ace::core::services::kernel_controller::iovec_allocate(128);
        if (not buf) {
            ace::terminate();
            co_return;
        }
        std::array<iovec, 1> iov {{{buf->iov}}};
        int n = co_await connection.recvmsg(iov);
        if (n > 0) {
            static_cast<char*>(buf->iov.iov_base)[n] = '\0';
            ace::console::println("Server [sg] received: '{}'", static_cast<char*>(buf->iov.iov_base));
        } else {
            ace::console::println("Server [sg] failed");
        }
        ace::core::services::kernel_controller::iovec_deallocate(*buf);
    }

    co_return;
}


inline ace::task tcp_echo_client() {

    auto bind_entry = co_await ace::net::io_socket_tcp();
    if (not bind_entry) {
        ace::console::println("[ CLIENT ERROR ] - {}", bind_entry.error());
        co_return;
    }

    ace::console::println("[ CLIENT ] - Socket created...");

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 8001);
    if (not selection_entry) {
        ace::console::println("[ CLIENT ERROR ] - {}", selection_entry.error());
        co_return;
    }

    ace::console::println("[ CLIENT ] - Socket bint...");

    const auto connection = co_await selection_entry.connect("127.0.0.1", 8000);
    if (not connection) {
        ace::console::println("[ CLIENT ERROR ] - {}", connection.error());
        co_return;
    }

    ace::console::println("[ CLIENT ] - Connected to server...");

    for (int i =1; i < 6; ++i) {
        if (const auto input = co_await ace::console::input(); not input)
            ace::console::println("[ CLIENT ERROR ] : {}", strerror(input.error()));
        else if (co_await connection.send(input.value()))
            ace::console::println("[ CLIENT SENT ] : {}", input.value());
    }

    co_return;
}

inline ace::task tcp_echo_server() {

    auto bind_entry = co_await ace::net::io_socket_tcp();
    if (not bind_entry) {
        ace::console::println("[ SERVER ERROR ] - {}", bind_entry.error());
        co_return;
    }

    ace::console::println("[ SERVER ] - Socket created...");

    auto selection_entry = co_await bind_entry.bind("127.0.0.1", 8000);
    if (not selection_entry) {
        ace::console::println("[ SERVER ERROR ] - {}", selection_entry.error());
        co_return;
    }

    ace::console::println("[ SERVER ] - Socket bint...");

    auto listener = co_await selection_entry.listen();
    if (not listener) {
        ace::console::println("[ SERVER ERROR ] - {}", listener.error());
        co_return;
    }

    ace::console::println("[ SERVER ] - Pending connections...");

    const auto connection = co_await listener.accept("127.0.0.1", 8001);
    if (not connection) {
        ace::console::println("[ SERVER ERROR ] - {}", connection.error());
        co_return;
    }

    ace::console::println("[ SERVER ] - Client connected...");

    std::array<char, 128> buff {};
    for (int i =0; i < 5; ++i) {
        if (co_await connection.recv(buff) > 0)
            ace::console::println("[ SERVER RECEIVED ] : {}", buff.data());
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
    ace::console::println("Placing {} to channel", idx);
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
        ace::console::println("spawn, post, spawn, post - finished {}", res);
    #endif

    ace::console::println("Placing {} to channel", 5);
    ch << 5;
    co_return;
}

inline ace::promise<int> pusher(int idx, ace::futures::channel_dyn<int>& ch) {
    ch << idx;
    co_return idx;
}

inline void printer(const int& idx) {
    ace::console::println("Placing {} to channel", idx);
}

inline ace::promise<> printer_promise(const int idx) {
    ace::console::println("Placing {} to channel", idx);
    co_return;
}

inline void congrats() {
    ace::console::println("Pipe finished");
}

inline ace::task composed_output(ace::futures::channel_dyn<int>& ch) {
    // NOTE: Starting parallel pipes
    co_await (
            pusher(1, ch) >> printer >> congrats
        and
            pusher(2, ch) >> printer_promise >> congrats
        and
            pusher(3, ch) >> printer >> congrats
        and
            pusher(4, ch) >> printer_promise >> congrats
        and
            pusher(5, ch) >> printer >> congrats
    );

}

inline ace::promise<int> wait_timer() {
    const auto wd = tool::lifetime("some_promise");
    ace::console::println("some_promise working...");
    co_await ace::futures::timeout(5ms);
    ace::console::println("{} finished", wd.mark());
    co_return 1;
}

inline ace::task or_with_async() {
    auto res = co_await ( wait_timer() or ace::futures::timeout(1ms) );
    if (not res)
        ace::console::println("timeout of promise");
}


inline ace::task fs_testing() {

    auto f = ace::fs::file("flexing.txt");
    if (auto f_entity = co_await f.open(O_CREAT | O_WRONLY | O_TRUNC))
        f_entity.writeln("testing flex {}", 1);
}

#endif // UNITS_H
