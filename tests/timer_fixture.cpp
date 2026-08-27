#include <chrono>
#include <cstddef>
#include <iostream>
#include <thread>
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
    struct relative_observation {
        int  id {};
        long requested_us {};
        long elapsed_us {};
    };

    struct absolute_observation {
        int id {};
        ace::services::timepoint_t deadline {};
        std::chrono::steady_clock::time_point woke_at {};
    };

    template <typename Rep, typename Period>
    static ace::task observe_timeout(
        int id,
        std::chrono::duration<Rep, Period> dur,
        ace::bus<relative_observation>& ch)
    {
        const auto start = std::chrono::steady_clock::now();
        co_await ace::timeout(dur);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        ch << relative_observation {
            id,
            std::chrono::duration_cast<std::chrono::microseconds>(dur).count(),
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
        };
        co_return;
    }

    template <typename Rep, typename Period>
    ace::task timer_waiter(std::chrono::duration<Rep, Period> dur,
                           ace::bus<int>& ch) {
        const auto start = std::chrono::steady_clock::now();
        co_await ace::timeout(dur);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        ch << static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        co_return;
    }

    static ace::task observe_expire(
        int id,
        ace::services::timepoint_t deadline,
        ace::bus<absolute_observation>& ch)
    {
        co_await ace::expire(deadline);
        ch << absolute_observation {id, deadline, std::chrono::steady_clock::now()};
        co_return;
    }

    static ace::task blocking_registration_timeout(ace::bus<relative_observation>& result) {
        // Blocking before registration ensures the timeout deadline is derived
        // from the actual subscribe point rather than an older cached timestamp.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        co_await observe_timeout(0, std::chrono::milliseconds(10), result);
    }

    static ace::task register_while_release_budget_is_exhausted(
        ace::bus<relative_observation>& result)
    {
        co_await ace::timeout(std::chrono::milliseconds(10));
        co_await observe_timeout(0, std::chrono::milliseconds(10), result);
    }

    static ace::task delayed_absolute_race(ace::bus<int>& result) {
        const auto deadline = std::chrono::ceil<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() + std::chrono::milliseconds(10));
        auto absolute = ace::expire(deadline);

        // The absolute deadline is in the past before co_await. Converting it
        // to a relative delay at construction would let the 5 ms branch win.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        int winner = co_await (absolute or ace::timeout(std::chrono::milliseconds(5)));
        result << winner;
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
    // The clock contract forbids completion before the longer branch's deadline.
    EXPECT_GE(ms_time, 100);
}

// Verifies that each scheduled timeout fires within its per-timer tolerance.
TEST_F(timer_fixture, do_timer_on_runner_test) {
    using namespace std::chrono_literals;
    const std::vector<long> expected {
        501, 495, 450, 401, 395, 350, 300, 256, 250, 200, 150, 100, 50, 10, 0
    };
    ace::bus<relative_observation> observations;
    for (std::size_t id = 0; id < expected.size(); ++id)
        ace::schedule(observe_timeout(
            static_cast<int>(id), std::chrono::milliseconds(expected[id]), observations));
    ace::run();
    ASSERT_TRUE(ace::empty());

    auto res = fetch(observations);
    ASSERT_EQ(expected.size(), res.size());
    for (const auto& observation : res) {
        ASSERT_GE(observation.id, 0);
        ASSERT_LT(static_cast<std::size_t>(observation.id), expected.size());
        // Matching by ID proves every individual timer completed and its
        // externally observed elapsed time did not precede its own request.
        EXPECT_GE(observation.elapsed_us, observation.requested_us);
        EXPECT_LT(observation.elapsed_us, observation.requested_us + 100000);
    }
}

// Verifies that every absolute deadline is delivered by expire().
TEST_F(timer_fixture, do_expire_on_runner_test) {
    using namespace std::chrono_literals;
    const auto now = std::chrono::ceil<std::chrono::milliseconds>(
        std::chrono::steady_clock::now());
    std::vector<ace::services::timepoint_t> expected;
    ace::bus<absolute_observation> observations;
    for (long d : {501l, 495l, 450l, 401l, 395l, 350l, 300l, 256l,
                   250l, 200l, 150l, 100l, 50l, 10l, 0l}) {
        expected.push_back(now + std::chrono::milliseconds(d));
        ace::schedule(observe_expire(
            static_cast<int>(expected.size() - 1), expected.back(), observations));
    }
    ace::run();
    ASSERT_TRUE(ace::empty());

    auto res = fetch(observations);
    ASSERT_EQ(expected.size(), res.size());
    for (const auto& observation : res) {
        ASSERT_GE(observation.id, 0);
        ASSERT_LT(static_cast<std::size_t>(observation.id), expected.size());
        EXPECT_EQ(expected[observation.id], observation.deadline);
        // The precise wake timestamp is compared directly with the requested
        // absolute deadline, so publishing the input cannot make this pass.
        EXPECT_GE(observation.woke_at, observation.deadline);
    }
}

// Verifies high-volume timer delivery across four runners.
TEST_F(timer_fixture, do_timer_on_runner_parallel_test) {
    using namespace std::chrono_literals;
    ace::cfg::g_config._runners_amount = 4;
    ace::reload();
    constexpr long sets_count = 110;
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
        for (int q = set_step; q <= max_in_set; q += set_step)
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
    EXPECT_GE(res[0], 10);
    // The generous upper bound detects a stuck timer while tolerating loaded CI hosts.
    EXPECT_LT(res[0], 500);
}

// Verifies that a timestamp sampled before blocking user code cannot shorten a later timeout.
TEST_F(timer_fixture, timeout_uses_registration_timestamp) {
    ace::bus<relative_observation> observations;
    ace::schedule(blocking_registration_timeout(observations));
    ace::run();
    ASSERT_TRUE(ace::empty());

    const auto res = fetch(observations);
    ASSERT_EQ(1u, res.size());
    EXPECT_GE(res[0].elapsed_us, res[0].requested_us);
}

// Verifies that a lagging wheel cursor cannot shorten a newly registered timeout.
TEST_F(timer_fixture, timeout_while_release_budget_is_exhausted) {
    ace::bus<relative_observation> observations;
    ace::bus<int> bulk_results;
    ace::schedule(register_while_release_budget_is_exhausted(observations));
    for (int i = 0; i < 1100; ++i)
        ace::schedule(timer_waiter(std::chrono::milliseconds(10), bulk_results));

    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_EQ(1100u, fetch(bulk_results).size());
    const auto result = fetch(observations);
    ASSERT_EQ(1u, result.size());
    // The fresh registration timestamp, rather than a lagging release cursor,
    // defines the lower bound for the newly created timeout.
    EXPECT_GE(result[0].elapsed_us, result[0].requested_us);
}

// Verifies that a positive sub-millisecond timeout rounds up instead of becoming immediate.
TEST_F(timer_fixture, timeout_positive_submillisecond_never_completes_early) {
    ace::bus<relative_observation> observations;
    ace::schedule(observe_timeout(0, std::chrono::microseconds(500), observations));
    ace::run();
    ASSERT_TRUE(ace::empty());

    const auto res = fetch(observations);
    ASSERT_EQ(1u, res.size());
    EXPECT_GE(res[0].elapsed_us, 500);
}

// Verifies that expire preserves an absolute deadline across delayed co_await.
TEST_F(timer_fixture, expire_past_deadline_beats_new_relative_timeout) {
    ace::bus<int> result;
    ace::schedule(delayed_absolute_race(result));
    ace::run();
    ASSERT_TRUE(ace::empty());

    const auto res = fetch(result);
    ASSERT_EQ(1u, res.size());
    EXPECT_EQ(0, res[0]);
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
