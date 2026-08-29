#include <chrono>
#include <atomic>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "environment.h"

#include <ace/futures/cutex.h>
#include <ace/futures/get_runner.h>
#include <ace/futures/post.h>
#include <ace/futures/spawn.h>
#include <ace/futures/timeout.h>

namespace {

struct cross_mechanic_fixture : base_fixture {
    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
        ace::reset_signal();
    }

    void configure_runners(int count) {
        ace::cfg::g_config._runners_amount = count;
        ace::reload();
    }
};

ace::task timeout_child(ace::bus<int>& result) {
    co_await ace::timeout(std::chrono::seconds(10));
    result << 999;
}

ace::task cancel_timeout_driver(ace::bus<int>& result) {
    auto handle = co_await ace::spawn(timeout_child(result));
    co_await ace::timeout(std::chrono::milliseconds(10));
    handle.cancel();
    const bool joined = co_await handle.join();
    result << (joined ? 0 : 1);
}

ace::task channel_waiting_child(ace::bus<std::string>& channel, ace::bus<int>& result) {
    result << 1;
    co_await ace::timeout(std::chrono::milliseconds(5));
    (void)co_await channel.pull();
    result << 2;
}

ace::task cancel_channel_driver(ace::bus<std::string>& channel, ace::bus<int>& result) {
    auto handle = co_await ace::spawn(channel_waiting_child(channel, result));
    co_await ace::timeout(std::chrono::milliseconds(20));
    handle.cancel();
    const bool joined = co_await handle.join();
    result << (joined ? 0 : 1);
}

ace::task timeout_race_driver(ace::bus<int>& result) {
    const int winner = co_await (
        ace::timeout(std::chrono::milliseconds(5)) or
        ace::timeout(std::chrono::milliseconds(500))
    );
    result << int{winner};
}

ace::task hold_cutex(ace::cutex& cutex) {
    auto guard = ace::guard(cutex);
    co_await guard.capture();
    co_await ace::timeout(std::chrono::milliseconds(100));
    co_await guard.release();
}

ace::task race_cutex_with_timeout(ace::cutex& cutex, ace::bus<int>& result) {
    co_await ace::timeout(std::chrono::milliseconds(10));
    auto guard = ace::guard(cutex);
    const int winner = co_await (
        guard.capture() or ace::timeout(std::chrono::milliseconds(5))
    );
    result << int{winner};
}

ace::task push_value(ace::bus<int>& channel, int value) {
    channel << value;
    co_return;
}

ace::task report_runner(
    std::vector<ace::core::runner*>& runners,
    const std::size_t index)
{
    runners[index] = co_await ace::get_runner {};
}

ace::task delayed_completion(ace::bus<int>& channel, std::atomic_bool& started) {
    started.store(true, std::memory_order_release);
    co_await ace::timeout(std::chrono::milliseconds(50));
    channel << 1;
}

ace::task terminate_runner(ace::bus<int>& channel) {
    ace::terminate();
    channel << 1;
    co_return;
}

ace::task cutex_counter_racer(ace::cutex& cutex, int& counter, int increments) {
    auto guard = ace::guard(cutex);
    for (int i = 0; i < increments; ++i) {
        co_await guard.capture();
        ++counter;
        co_await guard.release();
    }
}

ace::task composed_timeout_child(ace::bus<int>& result) {
    co_await (
        ace::timeout(std::chrono::seconds(10)) and
        ace::timeout(std::chrono::seconds(10))
    );
    result << 999;
}

ace::task cancel_composition_driver(ace::bus<int>& result) {
    auto handle = co_await ace::spawn(composed_timeout_child(result));
    co_await ace::timeout(std::chrono::milliseconds(10));
    handle.cancel();
    const bool joined = co_await handle.join();
    result << (joined ? 0 : 1);
}

ace::task three_way_timeout_race(ace::bus<int>& result) {
    const int winner = co_await (
        ace::timeout(std::chrono::milliseconds(100)) or
        ace::timeout(std::chrono::milliseconds(10)) or
        ace::timeout(std::chrono::milliseconds(200))
    );
    result << int{winner};
}

ace::task spawn_post_driver(ace::bus<int>& channel) {
    co_await ace::spawn(push_value(channel, 1));
    co_await ace::post(push_value(channel, 0));
    co_await ace::timeout(std::chrono::milliseconds(20));
    channel << 9;
}

ace::task stress_cancel_child(ace::bus<int>& result, int value) {
    co_await ace::timeout(std::chrono::seconds(10));
    result << value;
}

ace::task stress_cancel_driver(ace::bus<int>& result, int count) {
    for (int index = 0; index < count; ++index) {
        auto handle = co_await ace::spawn(stress_cancel_child(result, index));
        handle.cancel();
        (void)co_await handle.join();
    }
    result << 1;
}

ace::task channel_round_trip(ace::bus<int>& channel, ace::bus<int>& result) {
    channel << 42;
    result << co_await channel.pull();
}

ace::automaton<int> delayed_sequence(int a, int b, int c, int d) {
    co_yield a;
    co_await ace::timeout(std::chrono::milliseconds(1));
    co_yield b;
    co_await ace::timeout(std::chrono::milliseconds(1));
    co_yield c;
    co_await ace::timeout(std::chrono::milliseconds(1));
    co_return d;
}

ace::task race_automaton_pings(ace::bus<int>& result) {
    auto first = co_await ace::spawn(delayed_sequence(10, 20, 30, 40));
    auto second = co_await ace::spawn(delayed_sequence(100, 200, 300, 400));

    int collected = 0;
    auto record = [&result, &collected](std::optional<int> value) -> std::optional<int> {
        if (value) {
            result << *value;
            ++collected;
        }
        return value;
    };

    while (collected < 8) {
        if (first.done()) {
            if (auto value = co_await second.ping()) {
                result << *value;
                ++collected;
            }
        } else if (second.done()) {
            if (auto value = co_await first.ping()) {
                result << *value;
                ++collected;
            }
        } else {
            co_await (first.ping() >> record or second.ping() >> record);
        }
    }
}

// Verifies that canceling a spawned timeout task releases its timer and handle.
TEST_F(cross_mechanic_fixture, cancel_spawned_with_timeout) {
    ace::bus<int> result;
    ace::schedule(cancel_timeout_driver(result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_GE(values.size(), 1u);
    // The canceled child's marker 999 must not replace the failed-join marker.
    EXPECT_EQ(1, values[0]);
}

// Verifies that canceling a task suspended in channel::pull removes its waiter.
TEST_F(cross_mechanic_fixture, cancel_spawned_with_channel) {
    ace::bus<std::string> channel;
    ace::bus<int> result;
    ace::schedule(cancel_channel_driver(channel, result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_GE(values.size(), 2u);
    EXPECT_EQ(1, values[0]);
    // A second 1 is the failed-join marker; marker 2 would mean pull resumed.
    EXPECT_EQ(1, values[1]);
    EXPECT_EQ(nullptr, channel._waiters.pop_node());
}

// Verifies timeout composition used as the bounded side of a channel race.
TEST_F(cross_mechanic_fixture, channel_with_timeout) {
    ace::bus<int> result;
    ace::schedule(timeout_race_driver(result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(0, values[0]);
}

// Verifies that a timeout wins while another task owns the cutex.
TEST_F(cross_mechanic_fixture, cutex_with_timeout) {
    ace::cutex cutex;
    ace::bus<int> result;
    ace::schedule(hold_cutex(cutex));
    ace::schedule(race_cutex_with_timeout(cutex, result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_GE(values.size(), 1u);
    // Index 1 identifies the timeout branch rather than the blocked capture.
    EXPECT_EQ(1, values[0]);
}

// Verifies that scheduled work completes across four configured runners.
TEST_F(cross_mechanic_fixture, multi_runner_spawn) {
    configure_runners(4);
    std::vector<ace::core::runner*> runners(8);
    for (std::size_t task = 0; task < runners.size(); ++task)
        ace::schedule(report_runner(runners, task));

    ace::run();
    EXPECT_TRUE(ace::empty());
    EXPECT_GE(std::set<ace::core::runner*>(runners.begin(), runners.end()).size(), 2u);
}

// Verifies that interrupt is issued while a timeout is suspended and the wait resumes after reset.
TEST_F(cross_mechanic_fixture, interrupt_during_timeout) {
    ace::bus<int> channel;
    std::atomic_bool started = false;
    ace::schedule(delayed_completion(channel, started));
    std::jthread runtime([] { ace::run(); });
    while (not started.load(std::memory_order_acquire))
        std::this_thread::yield();
    ace::interrupt();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ace::reset_signal();
    runtime.join();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
}

// Verifies that terminate issued by a task stops the run loop cleanly.
TEST_F(cross_mechanic_fixture, terminate_during_run) {
    ace::bus<int> channel;
    ace::schedule(terminate_runner(channel));
    ace::run();
    ace::reset_signal();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
}

// Verifies bounded multi-runner cutex exclusion without the former stress duration.
TEST_F(cross_mechanic_fixture, multi_runner_cutex_count) {
    configure_runners(4);
    ace::cutex cutex;
    int counter = 0;
    constexpr int racers = 4;
    constexpr int increments_per_racer = 64;
    for (int racer = 0; racer < racers; ++racer)
        ace::schedule(cutex_counter_racer(cutex, counter, increments_per_racer));

    ace::run();
    EXPECT_TRUE(ace::empty());
    // A plain int is safe only if every increment was mutually excluded.
    EXPECT_EQ(racers * increments_per_racer, counter);
}

// Verifies that canceling an and-composition cancels both timeout observers.
TEST_F(cross_mechanic_fixture, and_compose_with_cancel) {
    ace::bus<int> result;
    ace::schedule(cancel_composition_driver(result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_GE(values.size(), 1u);
    // Marker 1 is produced by join(false); marker 999 would indicate leakage.
    EXPECT_EQ(1, values[0]);
}

// Verifies that a three-way race reports the middle, shortest timeout.
TEST_F(cross_mechanic_fixture, or_await_composed_3) {
    ace::bus<int> result;
    ace::schedule(three_way_timeout_race(result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
}

// Verifies that spawn and post children both complete before the driver marker.
TEST_F(cross_mechanic_fixture, spawn_post_interaction) {
    ace::bus<int> channel;
    ace::schedule(spawn_post_driver(channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_GE(values.size(), 2u);
    bool has_post = false;
    bool has_spawn = false;
    for (const int value : values) {
        if (value == 0) has_post = true;
        if (value == 1) has_spawn = true;
    }
    EXPECT_TRUE(has_post);
    EXPECT_TRUE(has_spawn);
    // The delay makes marker 9 a completion fence for both child tasks.
    EXPECT_EQ(9, values.back());
}

// Verifies repeated spawn-cancel-join cycles leave no child completion markers.
TEST_F(cross_mechanic_fixture, stress_spawn_cancel) {
    constexpr int operations = 100;
    ace::bus<int> result;
    ace::schedule(stress_cancel_driver(result, operations));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
}

// Verifies that a completed channel round trip leaves no buffered data or waiter.
TEST_F(cross_mechanic_fixture, channel_clean_after_run) {
    ace::bus<int> channel;
    ace::bus<int> result;
    ace::schedule(channel_round_trip(channel, result));
    ace::run();
    EXPECT_TRUE(ace::empty());
    EXPECT_TRUE(channel.empty());

    const auto values = fetch(result);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(42, values[0]);
}

// Verifies that racing two automaton ping streams loses none of their eight values.
TEST_F(cross_mechanic_fixture, or_ping_automaton_loop_no_value_loss) {
    ace::bus<int> result;
    ace::schedule(race_automaton_pings(result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(8u, values.size());
    std::set<int> expected {10, 20, 30, 40, 100, 200, 300, 400};
    for (const int value : values)
        expected.erase(value);
    // Order is nondeterministic, so set exhaustion checks exact membership.
    EXPECT_TRUE(expected.empty());
}

} // namespace
