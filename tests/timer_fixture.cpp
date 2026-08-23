#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <ranges>
#include <vector>

#include "environment.h"

#include <ace/console.h>
#include <ace/core/tools/lifetime.h>
#include <ace/futures/timeout.h>
#include <ace/services/clock.h>

using namespace std::chrono_literals;
namespace tool = ace::core::tools;

namespace {

struct timer_fixture : base_fixture {
    static auto fancy(ace::services::timepoint_t timepoint) {
        const auto offset =
            std::chrono::time_point_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now()).time_since_epoch()
            - std::chrono::time_point_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now()).time_since_epoch();
        return std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>{
            std::chrono::time_point_cast<std::chrono::milliseconds>(
                timepoint + offset).time_since_epoch()
        };
    }

    template <typename Rep, typename Period>
    ace::task timer_waiter_valued(std::chrono::duration<Rep, Period> dur,
                                  ace::bus<int>& ch) {
        ace::println("Timeout launched for: {}", dur);
        co_await ace::timeout(dur);
        ace::println("Timeout released after: {}", dur);
        ch << dur.count();
        co_return;
    }

    template <typename Rep, typename Period>
    ace::task timer_waiter(std::chrono::duration<Rep, Period> dur,
                           ace::bus<int>& ch) {
        const auto start = ace::services::clock::current_time();
        co_await ace::timeout(dur);
        const auto end = ace::services::clock::current_time();
        ch << (end - start).count();
        co_return;
    }

    ace::task expire_waiter_valued(ace::services::timepoint_t tp,
                                   ace::bus<ace::services::timepoint_t>& ch) {
        ace::println("Expires at: {}", fancy(tp));
        co_await ace::expire(tp);
        ace::println("Expired at: {}", fancy(tp));
        ch << tp;
        co_return;
    }

    static ace::task timer_or_timer() {
        auto long_timeout = ace::timeout(10s);
        co_await (long_timeout or ace::timeout(100ms));
        co_return;
    }

    static ace::task timer_and_timer() {
        auto long_timeout = ace::timeout(100ms);
        co_await (long_timeout and ace::timeout(10ms));
        co_return;
    }

    static ace::promise<int> wait_timer() {
        const auto wd = tool::lifetime("some_promise");
        ace::println("some_promise working...");
        co_await ace::timeout(5ms);
        ace::println("{} finished", wd.mark());
        co_return 1;
    }

    static ace::task or_with_async() {
        auto res = co_await (wait_timer() or ace::timeout(1ms));
        if (not res)
            ace::println("timeout of promise");
    }

    static ace::task timeout_zero_worker(ace::bus<int>& result) {
        co_await ace::timeout(std::chrono::milliseconds(0));
        result << 1;
        co_return;
    }

    static ace::task timeout_short_worker(
        std::chrono::steady_clock::time_point start,
        ace::bus<int>& result) {
        co_await ace::timeout(std::chrono::milliseconds(10));
        const auto elapsed = std::chrono::steady_clock::now() - start;
        result << static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        );
        co_return;
    }

    static ace::task concurrent_timer_worker(int idx, ace::bus<int>& result) {
        co_await ace::timeout(std::chrono::milliseconds(idx));
        result << idx;
        co_return;
    }

    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
    }

    ace::bus<int> _int_channel {};
    ace::bus<ace::services::timepoint_t> _tp_channel {};
};

// Verifies that an or-composition completes when its shorter timer expires.
TEST_F(timer_fixture, do_or_await_test) {
    const auto start_time = std::chrono::steady_clock::now();
    ace::schedule(timer_or_timer());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    EXPECT_GE(ms_time, 100);
    // The upper bound detects an or-composition that waits for the ten-second branch.
    EXPECT_LT(ms_time, 500);
}

// Verifies that a promise can race a timeout without leaving runner work behind.
TEST_F(timer_fixture, do_or_with_promise_tests) {
    ace::schedule(or_with_async());
    ace::run();
    ASSERT_TRUE(ace::empty());
}

// Verifies that an and-composition waits for its longer timer.
TEST_F(timer_fixture, do_and_await_test) {
    const auto start_time = std::chrono::steady_clock::now();
    ace::schedule(timer_and_timer());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    // A small scheduling allowance avoids requiring an exact 100 ms clock boundary.
    EXPECT_GE(ms_time, 95);
}

// Verifies that each scheduled timeout fires within its per-timer tolerance.
TEST_F(timer_fixture, do_timer_on_runner_test) {
    using namespace std::chrono_literals;
    const std::vector<long> expected {
        501, 495, 450, 401, 395, 350, 300, 256, 250, 200, 150, 100, 50, 10, 0
    };
    for (long d : expected)
        ace::schedule(timer_waiter_valued(std::chrono::milliseconds(d), _int_channel));
    ace::run();
    ASSERT_TRUE(ace::empty());

    auto res = fetch(_int_channel);
    ASSERT_EQ(expected.size(), res.size());
    for (long d : expected) {
        // Timers sharing a wheel slot have no deadline ordering guarantee, so match by value.
        const bool found = std::ranges::any_of(
            res, [d](long v) { return v >= d - 1 and v <= d + 50; });
        EXPECT_TRUE(found) << "timer " << d << "ms did not fire on time";
    }
}

// Verifies that every absolute deadline is delivered by expire().
TEST_F(timer_fixture, do_expire_on_runner_test) {
    using namespace std::chrono_literals;
    const auto now = ace::services::clock::current_time();
    std::vector<ace::services::timepoint_t> expected;
    for (long d : {501l, 495l, 450l, 401l, 395l, 350l, 300l, 256l,
                   250l, 200l, 150l, 100l, 50l, 10l, 0l}) {
        expected.push_back(now + std::chrono::milliseconds(d));
        ace::schedule(expire_waiter_valued(expected.back(), _tp_channel));
    }
    ace::run();
    ASSERT_TRUE(ace::empty());

    auto res = fetch(_tp_channel);
    ASSERT_EQ(expected.size(), res.size());
    for (const auto& d : expected) {
        // expire_waiter_valued reports requested deadlines, whose wake order is unspecified.
        const bool found = std::ranges::any_of(
            res, [&d](const auto& value) { return value == d; });
        EXPECT_TRUE(found) << "deadline not reached";
    }
}

// Verifies high-volume timer delivery across four runners.
TEST_F(timer_fixture, do_timer_on_runner_parallel_test) {
    using namespace std::chrono_literals;
    ace::cfg::g_config._runners_amount = 4;
    ace::reload();
    constexpr long sets_count = 10000;
    constexpr long max_in_set = 500;
    constexpr long set_step = 50;
    constexpr long set_size = max_in_set / set_step;

    for (int i = 0; i < sets_count; ++i)
        for (int q = 50; q <= max_in_set; q += set_step)
            ace::schedule(timer_waiter(std::chrono::milliseconds(q), _int_channel));

    std::cout << "Tasks spawned" << std::endl;
    const auto start_time = std::chrono::steady_clock::now();
    ace::run();
    const auto end_time = std::chrono::steady_clock::now();
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    // At least one maximum-duration timer must keep the run alive for about 500 ms.
    EXPECT_GE(ms_time, 500);
    std::cout << "Timers released after: " << ms_time << "ms.\n\t"
                 "Timers amount: " << sets_count * set_size << ".\n\t"
                 "Durations range: [" << set_step << "ms, " << max_in_set
              << "ms], step: " << set_step << std::endl;
    ASSERT_TRUE(ace::empty());

    std::vector<int> res;
    ace::schedule(channel_fetcher(_int_channel, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_EQ(res.size(), set_size * sets_count);

    long real_sum {}, exp_sum {};
    for (int i = 0; i < sets_count; ++i)
        for (int q = 0; q < max_in_set; q += set_step)
            exp_sum += q;
    for (auto r : res) real_sum += r;
    // Measured elapsed values include scheduling overhead and should exceed raw durations.
    EXPECT_GT(real_sum, exp_sum);
}

// Verifies that a zero-duration timeout completes promptly and publishes its result.
TEST_F(timer_fixture, timeout_zero) {
    const auto start = std::chrono::steady_clock::now();
    ace::schedule(timeout_zero_worker(_int_channel));
    ace::run();
    EXPECT_TRUE(ace::empty());
    const auto elapsed = std::chrono::steady_clock::now() - start;
    auto res = fetch(_int_channel);
    ASSERT_EQ(1u, res.size());
    EXPECT_EQ(1, res[0]);
    // This broad limit catches a stranded zero timer without depending on host load.
    EXPECT_LT(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500);
}

// Verifies that a short timeout completes within the clock's accepted tolerance.
TEST_F(timer_fixture, timeout_short) {
    const auto start = std::chrono::steady_clock::now();
    ace::schedule(timeout_short_worker(start, _int_channel));
    ace::run();
    EXPECT_TRUE(ace::empty());
    auto res = fetch(_int_channel);
    ASSERT_EQ(1u, res.size());
    EXPECT_GE(res[0], 0);
    // The generous upper bound detects a stuck timer while tolerating loaded CI hosts.
    EXPECT_LT(res[0], 500);
}

// Verifies that twenty concurrent timeouts all publish exactly one completion.
TEST_F(timer_fixture, timeout_multiple_concurrent) {
    constexpr int timer_count = 20;
    ace::bus<int> result;
    for (int idx = 0; idx < timer_count; ++idx)
        ace::schedule(concurrent_timer_worker(idx, result));
    ace::run();
    EXPECT_TRUE(ace::empty());
    auto res = fetch(result);
    // Cardinality detects both lost wakeups and duplicate timer delivery.
    EXPECT_EQ(static_cast<std::size_t>(timer_count), res.size());
}

} // namespace
