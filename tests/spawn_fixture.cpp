#include <chrono>
#include <concepts>
#include <optional>
#include <tuple>

#include "environment.h"

#include <ace/console.h>
#include <ace/core/tools/lifetime.h>
#include <ace/futures/get_runner.h>
#include <ace/futures/post.h>
#include <ace/futures/spawn.h>
#include <ace/futures/timeout.h>

using namespace std::chrono_literals;
namespace tool = ace::core::tools;

namespace {

struct spawn_fixture : base_fixture {
    ace::task to_spawn() {
        auto curr_runner = co_await ace::get_runner();
        co_await ace::timeout(100ms);
        ace::println("'spawned' runned out");
        _runner_channel << curr_runner;
        co_return;
    }

    ace::task spawner() {
        auto curr_runner = co_await ace::get_runner();
        _runner_channel << curr_runner;
        const auto handle = co_await ace::spawn(to_spawn());
        while (not handle.done()) {
            ace::println("'spawned' not done");
            co_await ace::timeout(10ms);
        }
        ace::println("'spawned' done!!!");
    }

    ace::async<int> valued_to_spawn() {
        auto curr_runner = co_await ace::get_runner();
        co_await ace::timeout(100ms);
        ace::println("'spawned' runned out");
        _runner_channel << curr_runner;
        co_return 123;
    }

    ace::task valued_spawner() {
        auto curr_runner = co_await ace::get_runner();
        _runner_channel << curr_runner;
        auto handle = co_await ace::spawn(valued_to_spawn());
        while (not handle.done()) {
            ace::println("'spawned' not done");
            co_await ace::timeout(10ms);
        }
        ace::println("'spawned' done with {} !!!", (co_await handle.join()).value());
    }

    ace::task join_spawner() {
        auto curr_runner = co_await ace::get_runner();
        _runner_channel << curr_runner;
        auto handle = co_await ace::spawn(to_spawn());
        ace::println("'spawned' is spawned");
        if (co_await handle.join())
            ace::println("'spawned' done!!!");
        else
            ace::println("'spawned' broken!!!");
    }

    ace::promise<> to_spawn_nested() {
        const auto wd = tool::lifetime("'parallel-nested'");
        ace::print("'parallel-nested' started\n");
        co_await ace::timeout(1000ms);
        _runner_channel << co_await ace::get_runner();
        ace::println("{} finished", wd.mark());
        co_return;
    }

    ace::task to_spawn_cancel() {
        const auto wd = tool::lifetime("'parallel'");
        ace::print("'parallel' started\n");
        co_await to_spawn_nested();
        co_await ace::timeout(1000ms);
        _runner_channel << co_await ace::get_runner();
        ace::println("{} finished", wd.mark());
        co_return;
    }

    ace::task spawner_cancel() {
        ace::println("'spawner' started");
        auto handle = co_await ace::spawn(to_spawn_cancel());
        co_await ace::timeout(100ms);
        ace::println("'spawner' awake, canceling...");
        handle.cancel();
        _runner_channel << co_await ace::get_runner();
        ace::println("'spawner' finished");
    }

    ace::task spawner_join_canceled() {
        ace::println("'spawner' started");
        auto handle = co_await ace::spawn(to_spawn_cancel());
        co_await ace::timeout(100ms);
        ace::println("'spawner' awake, canceling...");
        handle.cancel();
        if (not co_await handle.join())
            ace::println("'parallel' canceled. Joining is 'false'");
        else
            ace::println("'parallel' joined as alive. Failure");
        _runner_channel << co_await ace::get_runner();
        ace::println("'spawner' finished");
    }

    static ace::async<int> valued_long() {
        co_await ace::timeout(100ms);
        co_return 999;
    }

    static ace::task valued_spawner_cancel(ace::bus<int>& ch) {
        auto handle = co_await ace::spawn(valued_long());
        co_await ace::timeout(10ms);
        handle.cancel();
        auto joined = co_await handle.join();
        ch << (joined.has_value() ? 1 : 0);
        co_return;
    }

    static ace::async<int> valued_fast() {
        co_return 42;
    }

    static ace::task valued_spawner_join(ace::bus<int>& ch) {
        auto handle = co_await ace::spawn(valued_fast());
        while (not handle.done())
            co_await ace::timeout(1ms);
        auto res = co_await handle.join();
        ch << (res.has_value() ? res.value() : -1);
        co_return;
    }

    static ace::task spawn_post(int idx, ace::bus<int>& ch) {
        ace::println("Placing {} to channel", idx);
        ch << idx;
        co_return;
    }

    static ace::task imposter(ace::bus<int>& ch) {
        auto res = co_await (
                    (co_await ace::spawn(spawn_post(1, ch))).join()
                and (co_await ace::post (spawn_post(3, ch))).join()
                and (co_await ace::spawn(spawn_post(2, ch))).join()
                and (co_await ace::post (spawn_post(4, ch))).join()
        );
        static_assert(
            std::same_as<decltype(res), std::tuple<bool, bool, bool, bool>>,
            "Must be tuple of bools");
#if defined(__clang__) && __clang_major__ >= 22
        ace::println("spawn, post, spawn, post - finished {}", res);
#endif
        ace::println("Placing {} to channel", 5);
        ch << 5;
        co_return;
    }

    static ace::async<int> valued_spawn_post(int idx, ace::bus<int>& ch) {
        ace::println("Placing {} to channel", idx);
        ch << idx;
        co_return idx;
    }

    static ace::task valued_imposter(ace::bus<int>& ch) {
        auto res = co_await (
                    (co_await ace::spawn(valued_spawn_post(1, ch))).join()
                and (co_await ace::post (valued_spawn_post(3, ch))).join()
                and (co_await ace::spawn(valued_spawn_post(2, ch))).join()
                and (co_await ace::post (valued_spawn_post(4, ch))).join()
        );
        static_assert(
            std::same_as<
                decltype(res),
                std::tuple<std::optional<int>, std::optional<int>,
                           std::optional<int>, std::optional<int>>>,
            "Must be tuple of std::optional<int>s");
#if defined(__clang__) && __clang_major__ >= 22
        ace::println("spawn, post, spawn, post - finished {}", res);
#endif
        ace::println("Placing {} to channel", 5);
        ch << 5;
        ace::println("From 1'st: {}", std::get<0>(res).value());
        ace::println("From 2'st: {}", std::get<1>(res).value());
        ace::println("From 3'st: {}", std::get<2>(res).value());
        ace::println("From 4'st: {}", std::get<3>(res).value());
        co_return;
    }

    static ace::promise<int> pusher(int idx, ace::bus<int>& ch) {
        ch << idx;
        co_return idx;
    }

    static void printer(const int& idx) {
        ace::println("Placing {} to channel", idx);
    }

    static ace::promise<> printer_promise(const int idx) {
        ace::println("Placing {} to channel", idx);
        co_return;
    }

    static void congrats() {
        ace::println("Pipe finished");
    }

    static ace::task composed_output(ace::bus<int>& ch) {
        co_await (
                pusher(1, ch) >> printer >> congrats
            and pusher(2, ch) >> printer_promise >> congrats
            and pusher(3, ch) >> printer >> congrats
            and pusher(4, ch) >> printer_promise >> congrats
            and pusher(5, ch) >> printer >> congrats
        );
    }

    ace::bus<ace::core::runner*> _runner_channel {};
    ace::bus<int> _int_channel {};
};

// Verifies that spawn runs on the spawner's runner and reaches done().
TEST_F(spawn_fixture, check_spawn_command) {
    ace::schedule(spawner());
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(_runner_channel);
    ASSERT_EQ(res.size(), 2u);
    ASSERT_NE(res[0], nullptr);
    ASSERT_NE(res[1], nullptr);
    // Equal runner pointers prove spawn did not migrate the child unexpectedly.
    ASSERT_EQ(res[0], res[1]);
}

// Verifies post priority relative to spawn and the final continuation ordering.
TEST_F(spawn_fixture, check_post_command) {
    ace::schedule(imposter(_int_channel));
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 5u);
    // post attaches at the front, producing 3,1,4,2 before the parent emits 5.
    ASSERT_EQ(res[0], 3);
    ASSERT_EQ(res[1], 1);
    ASSERT_EQ(res[2], 4);
    ASSERT_EQ(res[3], 2);
    ASSERT_EQ(res[4], 5);
}

// Verifies that valued spawn completes on the same runner as its spawner.
TEST_F(spawn_fixture, check_valued_spawn_command) {
    ace::schedule(valued_spawner());
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(_runner_channel);
    ASSERT_EQ(res.size(), 2u);
    ASSERT_NE(res[0], nullptr);
    ASSERT_NE(res[1], nullptr);
    ASSERT_EQ(res[0], res[1]);
}

// Verifies post priority for four valued tasks joined through an and-composition.
TEST_F(spawn_fixture, check_valued_post_command) {
    ace::schedule(valued_imposter(_int_channel));
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 5u);
    // Valued carriers must preserve the same front/back queue ordering as void tasks.
    ASSERT_EQ(res[0], 3);
    ASSERT_EQ(res[1], 1);
    ASSERT_EQ(res[2], 4);
    ASSERT_EQ(res[3], 2);
    ASSERT_EQ(res[4], 5);
}

// Verifies that joining a canceled valued task returns std::nullopt.
TEST_F(spawn_fixture, check_valued_spawn_cancel) {
    ace::bus<int> ch;
    ace::schedule(valued_spawner_cancel(ch));
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(ch);
    ASSERT_EQ(res.size(), 1u);
    // The helper maps nullopt to zero so no internal control-block access is needed.
    ASSERT_EQ(res[0], 0);
}

// Verifies that joining a completed valued task returns its co_return value.
TEST_F(spawn_fixture, check_valued_spawn_join_value) {
    ace::bus<int> ch;
    ace::schedule(valued_spawner_join(ch));
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(ch);
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0], 42);
}

// Verifies all five pipe branches execute and publish values in source order.
TEST_F(spawn_fixture, check_composed_output) {
    ace::schedule(composed_output(_int_channel));
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 5u);
    ASSERT_EQ(res[0], 1);
    ASSERT_EQ(res[1], 2);
    ASSERT_EQ(res[2], 3);
    ASSERT_EQ(res[3], 4);
    ASSERT_EQ(res[4], 5);
}

// Verifies join suspends until a spawned task finishes on the same runner.
TEST_F(spawn_fixture, check_spawn_and_join) {
    ace::schedule(join_spawner());
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(_runner_channel);
    ASSERT_EQ(res.size(), 2u);
    ASSERT_NE(res[0], nullptr);
    ASSERT_NE(res[1], nullptr);
    ASSERT_EQ(res[0], res[1]);
}

// Verifies cancel stops a nested spawned task before its one-second timeout.
TEST_F(spawn_fixture, check_cancel) {
    const auto start_time = std::chrono::steady_clock::now();
    ace::schedule(spawner_cancel());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    auto res = fetch(_runner_channel);
    EXPECT_EQ(res.size(), 1u);
    EXPECT_NE(res[0], nullptr);
    // Finishing below 900 ms proves cancellation bypassed the nested 1000 ms wait.
    EXPECT_LT(ms_time, 900);
}

// Verifies join reports false after canceling a nested spawned task.
TEST_F(spawn_fixture, check_join_after_cancel) {
    const auto start_time = std::chrono::steady_clock::now();
    ace::schedule(spawner_join_canceled());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    auto res = fetch(_runner_channel);
    EXPECT_EQ(res.size(), 1u);
    EXPECT_NE(res[0], nullptr);
    EXPECT_LT(ms_time, 900);
}

} // namespace
