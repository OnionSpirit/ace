#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H


#include <memory>
#include <cstring>
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
// BaseFixture — shared utilities available to all test fixtures
// ==========================================================================

struct BaseFixture : ::testing::Test {

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
// ContextFixture — low-level coroutine tests (no runner)
// ==========================================================================

struct ContextFixture : BaseFixture {
    ace::promise<bool> simple_context_test() {
        BaseFixture::once_suspend tests_future;
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
// ChannelFixture — channel send/receive
// ==========================================================================

struct ChannelFixture : BaseFixture {
    ace::task channel_sender() {
        BaseFixture::once_suspend tests_future;
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
// TimerFixture — timer / expire / or / and tests
// ==========================================================================

struct TimerFixture : BaseFixture {
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
                           ace::futures::tunnel::dyn::bus<long>& ch) {
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

    ace::futures::tunnel::dyn::bus<int> _int_channel {};
    ace::futures::tunnel::dyn::bus<ace::services::timepoint_t> _tp_channel {};
};

// ==========================================================================
// TimerParallelFixture — heavy parallel timer test
// ==========================================================================

struct TimerParallelFixture : BaseFixture {
    void SetUp() override {
        ace::cfg::g_config._runners_amount = 4;
        ace::reload();
    }

    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
    }

    template <typename Rep, typename Period>
    ace::task timer_waiter(std::chrono::duration<Rep, Period> dur,
                           ace::futures::tunnel::dyn::bus<long>& ch) {
        const auto start = ace::services::clock::current_time();
        co_await ace::futures::timeout(dur);
        const auto end = ace::services::clock::current_time();
        ch << (end - start).count();
        co_return;
    }

    ace::futures::tunnel::dyn::bus<long> _channel {};
};

// ==========================================================================
// CutexFixture — cutex race + cancel tests (multi-runner)
// ==========================================================================

struct CutexFixture : BaseFixture {
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

    ace::task racer(const int max, std::string& counter) {
        ace::guard crx(_cutex);
        for (volatile int i = 0; i < max; i = i + 1) {
            co_await crx.capture();
            counter = std::to_string(std::stoi(counter) + 1);
            crx.sync();
            crx.sync(); // no-op check
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
// SpawnFixture — spawn / post / cancel / join / compose tests
// ==========================================================================

struct SpawnFixture : BaseFixture {
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
// SocketEchoFixture — TCP echo tests
// ==========================================================================

struct SocketEchoFixture : BaseFixture {
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
// FsFixture — filesystem test
// ==========================================================================

struct FsFixture : BaseFixture {
    ace::task fs_testing() {
        auto f = ace::fs::file("flexing.txt");
        if (auto f_entity = co_await f.open(O_CREAT | O_WRONLY | O_TRUNC))
            f_entity.writeln("testing flex {}", 1);
    }
};


#endif // ENVIRONMENT_H
