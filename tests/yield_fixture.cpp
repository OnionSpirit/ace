#include <chrono>
#include <optional>
#include <utility>

#include "environment.h"

#include <ace/console.h>
#include <ace/futures/post.h>
#include <ace/futures/spawn.h>
#include <ace/futures/timeout.h>

using namespace std::chrono_literals;

namespace {

struct yield_fixture : base_fixture {
    ace::automaton<int> num_auto() {
        ace::println("Yielding value: {}", 1);
        co_yield 1;
        ace::println("Yielding value: {}", 2);
        co_yield 2;
        ace::println("Yielding value: {}", 3);
        co_yield 3;
        ace::println("Yielding value: {}", 4);
        co_yield 4;
        ace::println("Yielding value: {}", 5);
        co_return 5;
    }

    ace::task auto_user() {
        auto at = num_auto();
        ace::println("Automaton inited");
        int res = co_await at;
        ace::println("Get from automaton: {}", res);
        _int_channel << res;
        res = co_await at;
        ace::println("Get from automaton: {}", res);
        _int_channel << res;
        res = co_await at;
        ace::println("Get from automaton: {}", res);
        _int_channel << res;
        res = co_await at;
        ace::println("Get from automaton: {}", res);
        _int_channel << res;
        res = co_await at;
        ace::println("Get from automaton: {}", res);
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
        co_await ace::timeout(5ms);
        co_yield 20;
        co_await ace::timeout(5ms);
        co_yield 30;
        co_return 99;
    }

    static auto push_to_channel(std::optional<int>&& val) -> std::optional<int> {
        _int_channel << val.value();
        val.reset();
        return val;
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

    static inline ace::bus<int> _int_channel {};
};

// Verifies direct automaton awaiting yields four values and its final return value.
TEST_F(yield_fixture, do_automaton_tests) {
    ace::schedule(auto_user());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 5u);
    EXPECT_EQ(res[0], 1);
    EXPECT_EQ(res[1], 2);
    EXPECT_EQ(res[2], 3);
    EXPECT_EQ(res[3], 4);
    EXPECT_EQ(res[4], 5);
}

// Verifies that repeated ping() returns all yields followed by the final value.
TEST_F(yield_fixture, spawn_automaton_ping) {
    ace::schedule(spawn_and_ping_test());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 4u);
    // This order distinguishes the three co_yield values from co_return 42.
    EXPECT_EQ(res[0], 1);
    EXPECT_EQ(res[1], 2);
    EXPECT_EQ(res[2], 3);
    EXPECT_EQ(res[3], 42);
}

// Verifies that automaton join consumes its first yield and then cancels the automaton.
TEST_F(yield_fixture, spawn_automaton_join) {
    ace::schedule(spawn_and_join_test());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 1u);
    // A single first value proves join did not continue through later yields.
    EXPECT_EQ(res[0], 1);
}

// Verifies that an automaton spawned with post() supports the same ping sequence.
TEST_F(yield_fixture, post_automaton_ping) {
    ace::schedule(post_and_ping_test());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 4u);
    EXPECT_EQ(res[0], 1);
    EXPECT_EQ(res[1], 2);
    EXPECT_EQ(res[2], 3);
    EXPECT_EQ(res[3], 42);
}

// Verifies that ping() returns nullopt after explicit automaton cancellation.
TEST_F(yield_fixture, spawn_automaton_cancel_ping_nullopt) {
    ace::schedule(spawn_cancel_ping_nullopt());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 1u);
    // The helper emits -1 only for the expected nullopt result.
    EXPECT_EQ(res[0], -1);
}

// Verifies that moving an automaton handle preserves access to subsequent yields.
TEST_F(yield_fixture, spawn_automaton_move_handle) {
    ace::schedule(spawn_move_handle());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 2u);
    EXPECT_EQ(res[0], 1);
    EXPECT_EQ(res[1], 2);
}

// Verifies that ping() crosses ordinary timeout awaits between automaton yields.
TEST_F(yield_fixture, spawn_automaton_ping_with_timeout) {
    ace::schedule(spawn_and_ping_with_timeout_test());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 4u);
    EXPECT_EQ(res[0], 10);
    EXPECT_EQ(res[1], 20);
    EXPECT_EQ(res[2], 30);
    EXPECT_EQ(res[3], 99);
}

// Verifies join returns nullopt if another ping consumes the yield it observed as pending.
TEST_F(yield_fixture, automaton_join_returns_nullopt_when_pending_yield_was_consumed) {
    auto automaton = yield_123_return_42();
    const auto control = automaton.observe();
    ace::core::async_handle<int, ace::core::automaton_rule> handle {control};

    // Drive the lazy automaton through its public runner interface until its
    // first yield is pending.  No awaiter protocol is skipped in this test.
    automaton.awake();
    auto pending_join = handle.join();
    ASSERT_TRUE(pending_join.await_ready());

    // A second valid ready/resume pair consumes the exact yield observed by
    // join.  This reproduces the B16 gap without manually calling
    // await_resume() on an awaiter that asked to suspend.
    auto competing_ping = handle.ping();
    ASSERT_TRUE(competing_ping.await_ready());
    EXPECT_EQ(std::optional<int> {1}, competing_ping.await_resume());

    // B16 requires the failed yield read to produce nullopt rather than the
    // uninitialised local value in automaton_join_handler::await_resume().
    EXPECT_EQ(std::nullopt, pending_join.await_resume());
    // join cancels every non-terminal automaton after its read attempt.
    EXPECT_TRUE(handle.done());
}

} // namespace
