#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H


#include <memory>
#include <cstring>
#include <span>
#include <unistd.h>
#include <gtest/gtest.h>
#include <ace/ace.h>
#include <ace/core/compose.h>
#include <ace/futures/get_runner.h>
#include <ace/futures/channel.h>
#include <ace/futures/timeout.h>
#include <ace/futures/cutex.h>
#include <ace/core/tools/lifetime.h>
#include <ace/core/traits/future.h>
#include <ace/console.h>
#include <ace/net.h>

namespace tool = ace::core::tools;

// ==========================================================================
// base_fixture — shared utilities available to all test fixtures
// ==========================================================================

struct base_fixture : ::testing::Test {

    struct once_suspend : ace::core::traits::busy_future_traits<once_suspend> {
        IMPORT_BUSY_FUTURE_ENV(once_suspend)
        bool _trigger { false };

        bool await_ready() override {
            if (not _trigger) { _trigger = true; return false; }
            return true;
        }
        void await_suspend(auto) {};
        auto await_resume() { }
    };

    template <typename T>
    static ace::task channel_fetcher(ace::futures::tunnel::dyn::bus<T>& ch, std::vector<T>& output) {
        std::vector<T> res {};
        while (not ch.empty()) { res.emplace_back(co_await ch.pull()); }
        output = std::move(res);
        co_return;
    }

    template <typename Rep, typename Period>
    static ace::task sleeper(std::chrono::duration<Rep, Period> wait_time) {
        co_await ace::futures::timeout(wait_time);
        co_return;
    }

    static auto fancy(ace::services::timepoint_t tp) {
        auto offset =
            std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch()
          - std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()).time_since_epoch();
        return std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>{
            std::chrono::time_point_cast<std::chrono::milliseconds>(tp + offset).time_since_epoch()
        };
    }

    template <typename T>
    std::vector<T> fetch(ace::futures::tunnel::dyn::bus<T>& ch) {
        std::vector<T> res;
        ace::schedule(channel_fetcher(ch, res));
        ace::run();
        EXPECT_TRUE(ace::empty());
        return res;
    }
};

// ==========================================================================
// context_fixture — low-level coroutine tests (no runner)
// ==========================================================================

struct context_fixture : base_fixture {
    ace::promise<bool> simple_context_test() {
        base_fixture::once_suspend tests_future;
        co_await tests_future;
        ace::console::println("One suspend complete");
        co_return true;
    }

    ace::task nested_context_suspender() {
        co_await simple_context_test();
        ace::console::println("Nested call complete");
        co_return;
    }
};

// ==========================================================================
// channel_fixture — channel send/receive
// ==========================================================================

struct channel_fixture : base_fixture {
    ace::task channel_sender() {
        base_fixture::once_suspend tests_future;
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

    ace::futures::tunnel::dyn::bus<std::string> _channel {};
};

// ==========================================================================
// timer_fixture — timer / expire / or / and tests
// ==========================================================================

struct timer_fixture : base_fixture {
    template <typename Rep, typename Period>
    ace::task timer_waiter_valued(std::chrono::duration<Rep, Period> dur,
                                  ace::futures::tunnel::dyn::bus<int>& ch) {
        ace::console::println("Timeout launched for: {}", dur);
        co_await ace::futures::timeout(dur);
        ace::console::println("Timeout released after: {}", dur);
        ch << dur.count();
        co_return;
    }

    template <typename Rep, typename Period>
    ace::task timer_waiter(std::chrono::duration<Rep, Period> dur,
                           ace::futures::tunnel::dyn::bus<int>& ch) {
        const auto start = ace::services::clock::current_time();
        co_await ace::futures::timeout(dur);
        const auto end = ace::services::clock::current_time();
        ch << (end - start).count();
        co_return;
    }

    ace::task expire_waiter_valued(ace::services::timepoint_t tp,
                                   ace::futures::tunnel::dyn::bus<ace::services::timepoint_t>& ch) {
        ace::console::println("Expires at: {}", fancy(tp));
        co_await ace::futures::expire(tp);
        ace::console::println("Expired at: {}", fancy(tp));
        ch << tp;
        co_return;
    }

    ace::task timer_or_timer() {
        auto long_timeout = ace::futures::timeout(10s);
        co_await (long_timeout or ace::futures::timeout(100ms));
        co_return;
    }

    ace::task timer_and_timer() {
        auto long_timeout = ace::futures::timeout(100ms);
        co_await (long_timeout and ace::futures::timeout(10ms));
        co_return;
    }

    ace::promise<int> wait_timer() {
        const auto wd = tool::lifetime("some_promise");
        ace::console::println("some_promise working...");
        co_await ace::futures::timeout(5ms);
        ace::console::println("{} finished", wd.mark());
        co_return 1;
    }

    ace::task or_with_async() {
        auto res = co_await (wait_timer() or ace::futures::timeout(1ms));
        if (not res)
            ace::console::println("timeout of promise");
    }

    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
    }

    ace::futures::tunnel::dyn::bus<int> _int_channel {};
    ace::futures::tunnel::dyn::bus<ace::services::timepoint_t> _tp_channel {};
};

// ==========================================================================
// yield_fixture — automaton tests
// ==========================================================================

struct yield_fixture : base_fixture {

    ace::automaton<int> num_auto() {
        ace::console::println("Yielding value: {}", 1);
        co_yield 1;
        ace::console::println("Yielding value: {}", 2);
        co_yield 2;
        ace::console::println("Yielding value: {}", 3);
        co_yield 3;
        ace::console::println("Yielding value: {}", 4);
        co_yield 4;
        ace::console::println("Yielding value: {}", 5);
        co_return 5;
    }

    ace::task auto_user() {
        auto at = num_auto();
        ace::console::println("Automaton inited");
        int res = co_await at;
        ace::console::println("Get from automaton: {}", res);
        _int_channel << res;
        res = co_await at;
        ace::console::println("Get from automaton: {}", res);
        _int_channel << res;
        res = co_await at;
        ace::console::println("Get from automaton: {}", res);
        _int_channel << res;
        res = co_await at;
        ace::console::println("Get from automaton: {}", res);
        _int_channel << res;
        res = co_await at;
        ace::console::println("Get from automaton: {}", res);
        _int_channel << res;
    }

    static ace::automaton<int> yield_123_return_42() {
        co_yield 1;
        co_yield 2;
        co_yield 3;
        co_return 42;
    }

    static ace::automaton<int> yield_with_timeout() {
        co_yield 10;
        co_await ace::futures::timeout(5ms);
        co_yield 20;
        co_await ace::futures::timeout(5ms);
        co_yield 30;
        co_return 99;
    }

    static ace::task spawn_and_ping_test() {
        auto handle = co_await ace::spawn(yield_123_return_42());
        (co_await handle.ping()).and_then(push_to_channel);
        (co_await handle.ping()).and_then(push_to_channel);
        (co_await handle.ping()).and_then(push_to_channel);
        (co_await handle.ping()).and_then(push_to_channel);
    }

    static ace::task spawn_and_join_test() {
        auto handle = co_await ace::spawn(yield_123_return_42());
        // join = ping next value, then cancel
        (co_await handle.join()).and_then(push_to_channel);
    }

    static ace::task spawn_and_ping_with_timeout_test() {
        auto handle = co_await ace::spawn(yield_with_timeout());
        (co_await handle.ping()).and_then(push_to_channel);
        (co_await handle.ping()).and_then(push_to_channel);
        (co_await handle.ping()).and_then(push_to_channel);
        (co_await handle.ping()).and_then(push_to_channel);
    }

    static ace::task post_and_ping_test() {
        auto handle = co_await ace::post(yield_123_return_42());
        (co_await handle.ping()).and_then(push_to_channel);
        (co_await handle.ping()).and_then(push_to_channel);
        (co_await handle.ping()).and_then(push_to_channel);
        (co_await handle.ping()).and_then(push_to_channel);
    }

    static ace::task spawn_cancel_ping_nullopt() {
        auto handle = co_await ace::spawn(yield_123_return_42());
        handle.cancel();
        auto result = co_await handle.ping();
        if (not result.has_value())
            _int_channel << -1;
    }

    static ace::task spawn_move_handle() {
        auto h1 = co_await ace::spawn(yield_123_return_42());
        auto val = (co_await h1.ping()).value();
        _int_channel << val;
        auto h2 = std::move(h1);
        val = (co_await h2.ping()).value();
        _int_channel << val;
    }

    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
    }

    static inline ace::futures::tunnel::dyn::bus<int> _int_channel {};

    static auto push_to_channel (std::optional<int>&& val) -> std::optional<int> {
        _int_channel << val.value(); val.reset(); return val;
    };
};

// ==========================================================================
// cutex_fixture — cutex race + cancel tests (multi-runner)
// ==========================================================================

struct cutex_fixture : base_fixture {
    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
        ace::reset_signal();
    }

    void configure_runners(int n) {
        _runners = n;
        ace::cfg::g_config._runners_amount = n;
        ace::reload();
    }

    // ── race helpers ──

    ace::task capture_racer(const int max, std::string& counter) {
        ace::guard crx(_cutex);
        for (volatile int i = 0; i < max; i = i + 1) {
            co_await crx.capture();
            counter = std::to_string(std::stoi(counter) + 1);
            co_await crx.release();
            co_await crx.release(); // no-op check
        }
        co_await crx.capture();
        ace::console::println("'racer' finished");
    }

    ace::task sync_racer(const int max, std::string& counter) {
        ace::guard crx(_cutex);
        for (volatile int i = 0; i < max; i = i + 1) {
            co_await crx.sync();
            counter = std::to_string(std::stoi(counter) + 1);
            co_await crx.release();
            co_await crx.release(); // no-op check
        }
        co_await crx.capture();
        ace::console::println("'racer' finished");
    }

    // ── cancel helpers ──

    ace::task cutex_parallel() {
        ace::console::println("'cutex_parallel' started");
        const auto wd = tool::lifetime("'cutex_parallel'");
        ace::guard crx(_cutex);
        co_await crx.capture();
        co_await ace::futures::timeout(50ms);
        _runner_channel << co_await ace::get_runner();
        ace::console::println("{} finished", wd.mark());
    }

    ace::task cutex_carry() {
        ace::console::println("'cutex_carry' started");
        const auto wd = tool::lifetime("'cutex_carry'");
        ace::guard crx(_cutex);
        co_await crx.capture();
        ace::console::println("'cutex_carry' captured cutex");
        co_await ace::futures::timeout(100ms);
        _runner_channel << co_await ace::get_runner();
        ace::console::println("{} finished", wd.mark());
    }

    ace::task cutex_checker() {
        ace::guard crx(_cutex);
        if (co_await (crx.capture() or ace::futures::timeout(50ms)) == 0) {
            ace::console::println("'cutex_checker' captured cutex");
            _runner_channel << co_await ace::get_runner();
            ace::console::println("'cutex_checker' finished");
            co_return;
        }
        ace::console::println("'cutex_checker' can't capture cutex. FAILED");
    }

    ace::task cutex_spawner() {
        ace::console::println("'cutex_spawner' started");
        co_await ace::futures::timeout(10ms);
        auto handle = co_await ace::spawn(cutex_carry());
        co_await ace::futures::timeout(75ms);
        ace::console::println("'cutex_spawner' awake, canceling...");
        handle.cancel();
        co_await ace::futures::timeout(10ms);
        if (not co_await handle.join())
            ace::console::println("'cutex_carry' canceled. Joining is 'false'");
        else
            ace::console::println("'cutex_carry' joined as alive. Failure");
        _runner_channel << co_await ace::get_runner();
        ace::console::println("'cutex_spawner' finished");
    }

    ace::task cutex_spawner_permanent() {
        ace::console::println("'cutex_spawner_permanent' started");
        co_await ace::futures::timeout(10ms);
        auto handle = co_await ace::spawn(cutex_carry());
        co_await ace::futures::timeout(25ms);
        ace::console::println("'cutex_spawner_permanent' awake, canceling...");
        handle.cancel();
        co_await ace::futures::timeout(10ms);
        if (not co_await handle.join())
            ace::console::println("'cutex_carry' canceled. Joining is 'false'");
        else
            ace::console::println("'cutex_carry' joined as alive. Failure");
        _runner_channel << co_await ace::get_runner();
        ace::console::println("'cutex_spawner_permanent' finished");
    }

    ace::cutex _cutex {};
    ace::futures::tunnel::dyn::bus<ace::core::runner*> _runner_channel {};
    int _runners = 1;
};

// ==========================================================================
// spawn_fixture — spawn / post / cancel / join / compose tests
// ==========================================================================

struct spawn_fixture : base_fixture {
    // ── spawn / join ──

    ace::task to_spawn() {
        auto curr_runner = co_await ace::get_runner();
        co_await ace::futures::timeout(100ms);
        ace::console::println("'spawned' runned out");
        _runner_channel << curr_runner;
        co_return;
    }

    ace::task spawner() {
        auto curr_runner = co_await ace::get_runner();
        _runner_channel << curr_runner;
        const auto handle = co_await ace::spawn(to_spawn());
        while (not handle.done()) {
            ace::console::println("'spawned' not done");
            co_await ace::futures::timeout(10ms);
        }
        ace::console::println("'spawned' done!!!");
    }

    ace::async<int> valued_to_spawn() {
        auto curr_runner = co_await ace::get_runner();
        co_await ace::futures::timeout(100ms);
        ace::console::println("'spawned' runned out");
        _runner_channel << curr_runner;
        co_return 123;
    }

    ace::task valued_spawner() {
        auto curr_runner = co_await ace::get_runner();
        _runner_channel << curr_runner;
        auto handle = co_await ace::spawn(valued_to_spawn());
        while (not handle.done()) {
            ace::console::println("'spawned' not done");
            co_await ace::futures::timeout(10ms);
        }
        ace::console::println("'spawned' done with {} !!!", (co_await handle.join()).value());
    }

    ace::task join_spawner() {
        auto curr_runner = co_await ace::get_runner();
        _runner_channel << curr_runner;
        auto handle = co_await ace::spawn(to_spawn());
        ace::console::println("'spawned' is spawned");
        if (co_await handle.join()) ace::console::println("'spawned' done!!!");
        else ace::console::println("'spawned' broken!!!");
    }

    // ── cancel ──

    ace::promise<> to_spawn_nested() {
        const auto wd = tool::lifetime("'parallel-nested'");
        ace::console::print("'parallel-nested' started\n");
        co_await ace::futures::timeout(1000ms);
        _runner_channel << co_await ace::get_runner();
        ace::console::println("{} finished", wd.mark());
        co_return;
    }

    ace::task to_spawn_cancel() {
        const auto wd = tool::lifetime("'parallel'");
        ace::console::print("'parallel' started\n");
        co_await to_spawn_nested();
        co_await ace::futures::timeout(1000ms);
        _runner_channel << co_await ace::get_runner();
        ace::console::println("{} finished", wd.mark());
        co_return;
    }

    ace::task spawner_cancel() {
        ace::console::println("'spawner' started");
        auto handle = co_await ace::spawn(to_spawn_cancel());
        co_await ace::futures::timeout(100ms);
        ace::console::println("'spawner' awake, canceling...");
        handle.cancel();
        _runner_channel << co_await ace::get_runner();
        ace::console::println("'spawner' finished");
    }

    ace::task spawner_join_canceled() {
        ace::console::println("'spawner' started");
        auto handle = co_await ace::spawn(to_spawn_cancel());
        co_await ace::futures::timeout(100ms);
        ace::console::println("'spawner' awake, canceling...");
        handle.cancel();
        if (not co_await handle.join())
            ace::console::println("'parallel' canceled. Joining is 'false'");
        else ace::console::println("'parallel' joined as alive. Failure");
        _runner_channel << co_await ace::get_runner();
        ace::console::println("'spawner' finished");
    }

    // ── valued cancel ──

    ace::async<int> valued_long() {
        // Долгая valued-таска для тестирования cancel
        co_await ace::futures::timeout(100ms);
        co_return 999;
    }

    ace::task valued_spawner_cancel(ace::futures::tunnel::dyn::bus<int>& ch) {
        auto handle = co_await ace::spawn(valued_long());
        co_await ace::futures::timeout(10ms);
        handle.cancel();
        auto joined = co_await handle.join();
        // После cancel join должен вернуть nullopt
        ch << (joined.has_value() ? 1 : 0);
        co_return;
    }

    // ── valued join value check ──

    ace::async<int> valued_fast() {
        // Быстрая valued-таска: возвращает 42
        co_return 42;
    }

    ace::task valued_spawner_join(ace::futures::tunnel::dyn::bus<int>& ch) {
        auto handle = co_await ace::spawn(valued_fast());
        while (not handle.done()) {
            co_await ace::futures::timeout(1ms);
        }
        auto res = co_await handle.join();
        // join должен вернуть optional<int> со значением 42
        ch << (res.has_value() ? res.value() : -1);
        co_return;
    }

    // ── post / compose ──

    ace::task spawn_post(int idx, ace::futures::tunnel::dyn::bus<int>& ch) {
        ace::console::println("Placing {} to channel", idx);
        ch << idx;
        co_return;
    }

    ace::task imposter(ace::futures::tunnel::dyn::bus<int>& ch) {
        auto res = co_await (
                    (co_await ace::spawn(spawn_post(1, ch))).join()
                and (co_await ace::post (spawn_post(3, ch))).join()
                and (co_await ace::spawn(spawn_post(2, ch))).join()
                and (co_await ace::post (spawn_post(4, ch))).join()
        );
        static_assert(std::same_as<decltype(res), std::tuple<bool, bool, bool, bool>>, "Must be tuple of bools");
        #if defined(__clang__) && __clang_major__ >= 22
            ace::console::println("spawn, post, spawn, post - finished {}", res);
        #endif
        ace::console::println("Placing {} to channel", 5);
        ch << 5;
        co_return;
    }

    ace::async<int> valued_spawn_post(int idx, ace::futures::tunnel::dyn::bus<int>& ch) {
        ace::console::println("Placing {} to channel", idx);
        ch << idx;
        co_return idx;
    }

    ace::task valued_imposter(ace::futures::tunnel::dyn::bus<int>& ch) {
        auto res = co_await (
                    (co_await ace::spawn(valued_spawn_post(1, ch))).join()
                and (co_await ace::post (valued_spawn_post(3, ch))).join()
                and (co_await ace::spawn(valued_spawn_post(2, ch))).join()
                and (co_await ace::post (valued_spawn_post(4, ch))).join()
        );
        static_assert(std::same_as<decltype(res), std::tuple<std::optional<int>, std::optional<int>, std::optional<int>, std::optional<int>>>, "Must be tuple of std::optional<int>s");
        #if defined(__clang__) && __clang_major__ >= 22
                ace::console::println("spawn, post, spawn, post - finished {}", res);
        #endif
        ace::console::println("Placing {} to channel", 5);
        ch << 5;
        ace::console::println("From 1'st: {}", std::get<0>(res).value());
        ace::console::println("From 2'st: {}", std::get<1>(res).value());
        ace::console::println("From 3'st: {}", std::get<2>(res).value());
        ace::console::println("From 4'st: {}", std::get<3>(res).value());
        co_return;
    }

    // ── pipe / compose ──

    ace::promise<int> pusher(int idx, ace::futures::tunnel::dyn::bus<int>& ch) {
        ch << idx;
        co_return idx;
    }

    static void printer(const int& idx) {
        ace::console::println("Placing {} to channel", idx);
    }

    static ace::promise<> printer_promise(const int idx) {
        ace::console::println("Placing {} to channel", idx);
        co_return;
    }

    static void congrats() {
        ace::console::println("Pipe finished");
    }

    ace::task composed_output(ace::futures::tunnel::dyn::bus<int>& ch) {
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

    ace::futures::tunnel::dyn::bus<ace::core::runner*> _runner_channel {};
    ace::futures::tunnel::dyn::bus<int> _int_channel {};
};

// ==========================================================================
// socket_echo_fixture — TCP echo tests
// ==========================================================================

struct socket_echo_fixture : base_fixture {
    void TearDown() override {
        ace::reset_signal();
    }

    ace::task socket_listener() {
        auto bind_entry = co_await ace::net::socket_tcp();
        if (not bind_entry) { co_return; }
        auto selection_entry = co_await bind_entry.bind("127.0.0.1", _server_port);
        if (not selection_entry) { co_return; }
        auto listener = co_await selection_entry.listen();
        if (not listener) { co_return; }
        const auto connection = co_await listener.accept("127.0.0.1", _client_port);
        if (not connection) { co_return; }
        for (int i = 0; i < 5; ++i) {
            if (auto result = co_await connection.recv_buf())
                ace::console::println("Server received: '{}'", result.value().as<std::string>());
            else ace::console::println("Server failed: '{}'", strerror(result.error()));
        }
        co_return;
    }

    ace::task socket_abuser() {
        auto bind_entry = co_await ace::net::socket_tcp();
        if (not bind_entry) { co_return; }
        auto selection_entry = co_await bind_entry.bind("127.0.0.1", _client_port);
        if (not selection_entry) { co_return; }
        const auto connection = co_await selection_entry.connect("127.0.0.1", _server_port);
        if (not connection) { co_return; }
        for (int i = 1; i < 6; ++i) {
            std::string msg = "Echo message " + std::to_string(i);
            if (co_await connection.send(msg))
                ace::console::println("Client sent: '{}'", msg);
        }
        co_return;
    }

    ace::task socket_listener_zc() {
        auto bind_entry = co_await ace::net::socket_tcp();
        if (not bind_entry) { co_return; }
        auto selection_entry = co_await bind_entry.bind("127.0.0.1", _zc_server_port);
        if (not selection_entry) { co_return; }
        auto listener = co_await selection_entry.listen();
        if (not listener) { co_return; }
        const auto connection = co_await listener.accept("127.0.0.1", _zc_client_port);
        if (not connection) { co_return; }
        for (int i = 0; i < 5; ++i) {
            if (auto buf = co_await connection.recv_buf())
                ace::console::println("Server [zc] received: '{}'", buf.value());
            else
                ace::console::println("Server [zc] failed. error: {}", strerror(buf.error()));
        }
        co_return;
    }

    ace::task socket_abuser_zc() {
        auto bind_entry = co_await ace::net::socket_tcp();
        if (not bind_entry) { co_return; }
        auto selection_entry = co_await bind_entry.bind("127.0.0.1", _zc_client_port);
        if (not selection_entry) { co_return; }
        const auto connection = co_await selection_entry.connect("127.0.0.1", _zc_server_port);
        if (not connection) { co_return; }
        for (int i = 1; i < 6; ++i) {
            ace::io::buffer buf;
            buf.append("Echo message {}", i);
            if (co_await connection.send(buf) == EXIT_SUCCESS)
                ace::console::println("Client [zc] sent: '{}'", buf);
        }
        co_return;
    }

    static constexpr uint16_t _server_port    = 8000;
    static constexpr uint16_t _client_port    = 8001;
    static constexpr uint16_t _zc_server_port = 9000;
    static constexpr uint16_t _zc_client_port = 9001;
};

// ==========================================================================
// fs_fixture — filesystem test
// ==========================================================================

struct fs_fixture : base_fixture {
    ace::task fs_testing() {
        auto f = ace::fs::file("flexing.txt");
        if (auto f_entity = co_await f.open(O_CREAT | O_WRONLY | O_TRUNC))
            f_entity.writeln("testing flex {}", 1);
    }
};


// ==========================================================================
// queue_fixture — slab_mempool and intrusive queue tests
// ==========================================================================

struct queue_fixture : ::testing::Test {
    struct test_payload {
        int value;
        explicit test_payload(int v = 0) : value(v) {}
    };

    tool::slab_mempool<test_payload> _mempool {};
    tool::queue<test_payload> _queue { _mempool };
};

// ==========================================================================
// omniptr_fixture — type-agnostic pointer tests
// ==========================================================================

struct omniptr_fixture : ::testing::Test {};

// ==========================================================================
// id_alloc_fixture — lock-free ID allocator tests
// ==========================================================================

struct id_alloc_fixture : ::testing::Test {};

// ==========================================================================
// moving_average_fixture — sliding window average tests
// ==========================================================================

struct moving_average_fixture : ::testing::Test {};

// ==========================================================================
// future_traits_fixture — compile-time type trait tests
// ==========================================================================

struct future_traits_fixture : ::testing::Test {};

// ==========================================================================
// promise_traits_fixture — promise type behavior tests
// ==========================================================================

struct promise_traits_fixture : base_fixture {
    ace::promise<int> simple_valued_coroutine() {
        co_return 42;
    }

    ace::task simple_void_coroutine() {
        co_return;
    }
};

// ==========================================================================
// router_slot_fixture — router_slot in-place storage tests
// ==========================================================================

struct router_slot_fixture : ::testing::Test {
    // concrete test router for slot operations
    struct test_router : ace::core::traits::runner_router_handle<ace::omni_node> {
        bool _was_released = false;
        bool _was_canceled = false;
        static int alive_count; // track object lifetime for release tests

        test_router() { ++alive_count; }
        test_router(const test_router&) { ++alive_count; }
        test_router(test_router&&) noexcept { ++alive_count; }
        ~test_router() override { _was_released = true; --alive_count; }
        static void reset_counter() { alive_count = 0; }

        void redirect(ace::omni_node) override {}
        void cancel() override { _was_canceled = true; }
    };

    typedef ace::core::traits::router_slot<ace::core::traits::runner_router_handle<ace::omni_node>> slot_t;

    void TearDown() override {
        test_router::reset_counter();
    }
};

// ==========================================================================
// signal_fixture — signal handler tests
// ==========================================================================

struct signal_fixture : base_fixture {};

// ==========================================================================
// control_block_fixture — control block lifecycle tests
// ==========================================================================

struct control_block_fixture : ::testing::Test {
    // mini promise type for control block allocation tests
    // Использует реальный operator new из promise_traits для аллокации
    // control_block ПЕРЕД promise (как в production коде).
    struct mini_promise : ace::core::traits::promise_traits<mini_promise, ace::core::lazy_rule, void> {
        DECLARE_PROMISE_TRAITS(mini_promise, ace::core::lazy_rule, void)
        IMPORT_PROMISE_TRAITS_ENV

        mini_promise() = default;

        static auto initial_suspend() noexcept { return std::suspend_always{}; }
        static auto final_suspend() noexcept { return std::suspend_always{}; }
        void return_void() {}
        void unhandled_exception() {}

        auto get_return_object() noexcept {
            return ace::core::async<void, ace::core::lazy_rule>{};
        }
    };

    // Вручную аллоцирует control_block + mini_promise в одном буфере
    // (имитируя operator new из promise_traits).
    struct allocated_promise {
        ace::core::control_block* block;
        mini_promise* promise;

        allocated_promise() {
            constexpr std::size_t cb = sizeof(ace::core::control_block);
            constexpr std::size_t pm = sizeof(mini_promise);
            auto* raw = static_cast<uint8_t*>(::operator new(cb + pm));
            block = ::new (raw) ace::core::control_block();
            promise = ::new (raw + cb) mini_promise();
            promise->_block = block;
        }

        ~allocated_promise() {
            promise->~mini_promise();
            block->~control_block();
            ::operator delete(block);
        }

        auto get_handle() { return std::coroutine_handle<mini_promise>::from_promise(*promise); }
    };
};

// ==========================================================================
// runner_fixture — runner task management tests
// ==========================================================================

struct runner_fixture : base_fixture {
    ace::task dummy_task() {
        // задача-заглушка: немедленно завершается без сайд-эффектов
        co_return;
    }

    ace::task suspending_task(ace::futures::tunnel::dyn::bus<int>& ch) {
        // проверяем, что задача корректно обрабатывается раннером
        // ждём очень короткий таймаут чтобы гарантировать суспендирование
        co_await ace::futures::timeout(std::chrono::milliseconds(1));
        ch << 1;
        co_return;
    }
};

// ==========================================================================
// dispatcher_fixture — dispatcher lifecycle tests
// ==========================================================================

struct dispatcher_fixture : base_fixture {
    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
        ace::reset_signal();
    }

    ace::task simple_dispatched() {
        // минимальная задача для диспетчера
        co_return;
    }
};

// ==========================================================================
// io_buffer_fixture — io::buffer tests
// ==========================================================================

struct io_buffer_fixture : ::testing::Test {};

// ==========================================================================
// io_entity_fixture — io::entity + io::guard tests
// ==========================================================================

struct io_entity_fixture : ::testing::Test {};
// minimal entity for testing io::entity CRTP mechanics
struct test_io_entity : ace::io::entity<test_io_entity> {
    IMPORT_IO_ENTITY_ENV(test_io_entity)
    IMPORT_IO_ENTITY_FABRICATION
};

// ==========================================================================
// io_any_fixture — io::any tests
// ==========================================================================

struct io_any_fixture : ::testing::Test {};

// ==========================================================================
// io_hanged_fixture — io::hanged tests
// ==========================================================================

struct io_hanged_fixture : ::testing::Test {};

// ==========================================================================
// console_fixture — console output tests
// ==========================================================================

struct console_fixture : ::testing::Test {};

// ==========================================================================
// cross_mechanic_fixture — cross-subsystem interaction tests
// ==========================================================================

struct cross_mechanic_fixture : base_fixture {
    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
        ace::reset_signal();
    }

    void configure_runners(int n) {
        ace::cfg::g_config._runners_amount = n;
        ace::reload();
    }

    // Автоматоны с задержками между co_yield для проверки
    // or-гонки ping в цикле без потери значений
    static ace::automaton<int> gen_delayed_seq(int a, int b, int c, int d) {
        co_yield a;
        co_await ace::futures::timeout(std::chrono::milliseconds(1));
        co_yield b;
        co_await ace::futures::timeout(std::chrono::milliseconds(1));
        co_yield c;
        co_await ace::futures::timeout(std::chrono::milliseconds(1));
        co_return d;
    }

    static ace::task or_ping_two_in_loop(
        ace::futures::tunnel::dyn::bus<int>& result)
    {
        auto ha = co_await ace::spawn(gen_delayed_seq(10, 20, 30, 40));
        auto hb = co_await ace::spawn(gen_delayed_seq(100, 200, 300, 400));

        int collected = 0;
        auto grab_value = [&] (std::optional<int> val) -> std::optional<int> {
            if (val) {
                result << *val; ++collected;
                ace::console::println("Grabbed value form automaton - {}", *val);
            }
            return val;
        };

        while (collected < 8) {
            if (ha.done()) {
                if (auto val = co_await hb.ping()) {
                    result << *val;
                    ++collected;
                }
            } else if (hb.done()) {
                if (auto val = co_await ha.ping()) {
                    result << *val;
                    ++collected;
                }
            } else {
                co_await ( ha.ping() >> grab_value or hb.ping() >> grab_value );
            }
        }
        co_return;
    }
};

// ==========================================================================
// spawn_extra_fixture — extended spawn/post/reattach/roaming/polling tests
// ==========================================================================

struct spawn_extra_fixture : base_fixture {
    ace::futures::tunnel::dyn::bus<ace::core::runner*> _runner_channel {};

    ace::task post_checker(ace::futures::tunnel::dyn::bus<int>& ch) {
        // post task: должна быть обработана раньше spawn task
        ch << -1;
        co_return;
    }

    ace::task spawn_checker(int val, ace::futures::tunnel::dyn::bus<int>& ch) {
        ch << val;
        co_return;
    }
};

// ==========================================================================
// compose_extra_fixture — extended compose tests
// ==========================================================================

struct compose_extra_fixture : base_fixture {
    ace::promise<int> valued(int v) {
        co_await ace::futures::timeout(std::chrono::milliseconds(1));
        co_return v;
    }

    ace::task voided() {
        co_await ace::futures::timeout(std::chrono::milliseconds(1));
        co_return;
    }

    // composable function for operator>>
    static int double_it(int v) { return v * 2; }

    static ace::promise<int> double_async(int v) {
        co_await ace::futures::timeout(std::chrono::milliseconds(1));
        co_return v * 2;
    }
};

// ==========================================================================
// channel_extra_fixture — extended channel tests
// ==========================================================================

struct channel_extra_fixture : base_fixture {
    ace::futures::tunnel::dyn::bus<int> _ch {};

    ace::task pusher(int v) {
        _ch << v;
        co_return;
    }
};

// ==========================================================================
// cutex_extra_fixture — extended cutex tests
// ==========================================================================

struct cutex_extra_fixture : base_fixture {
    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
        ace::reset_signal();
    }

    ace::cutex _cutex {};
    ace::futures::tunnel::dyn::bus<int> _ch {};

    ace::task cutex_user(int id) {
        auto g = ace::guard(_cutex);
        co_await g.capture();
        _ch << id;
        g.release();
        co_return;
    }
};

// ==========================================================================
// get_runner_fixture — get_runner tests
// ==========================================================================

struct get_runner_fixture : base_fixture {
    ace::futures::tunnel::dyn::bus<int> _ch {};

    ace::task runner_gatherer() {
        auto r = co_await ace::get_runner();
        _ch << (r != nullptr ? 1 : 0);
        co_return;
    }
};

// Definition of static members declared in fixtures
inline int router_slot_fixture::test_router::alive_count = 0;

#endif // ENVIRONMENT_H
