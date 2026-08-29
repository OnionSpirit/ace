#include <algorithm>
#include <atomic>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ace/ace.h>
#include <ace/futures/channel.h>
#include <ace/futures/get_runner.h>

struct dispatcher_fixture : ::testing::Test {
    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
        ace::reset_signal();
    }

    static ace::task simple_dispatched() {
        co_return;
    }

    static ace::task push_value(ace::bus<int>& channel, const int value) {
        channel << int{value};
        co_return;
    }

    static ace::task increment(std::atomic_size_t& counter) {
        counter.fetch_add(1, std::memory_order_relaxed);
        co_return;
    }

    static ace::task report_runner(
        std::vector<ace::core::runner*>& runners,
        const std::size_t index)
    {
        runners[index] = co_await ace::get_runner {};
    }

    static ace::task keep_run_active(
        std::atomic_bool& started,
        std::atomic_bool& stop)
    {
        started.store(true, std::memory_order_release);
        while (not stop.load(std::memory_order_acquire))
            co_await ace::suspend {};
    }

    template <typename T>
    static ace::task drain_channel(ace::bus<T>& channel, std::vector<T>& output) {
        while (not channel.empty())
            output.emplace_back(co_await channel.pull());
    }

    template <typename T>
    static std::vector<T> fetch(ace::bus<T>& channel) {
        std::vector<T> output;
        ace::schedule(drain_channel(channel, output));
        ace::run();
        EXPECT_TRUE(ace::empty());
        return output;
    }
};

// Verifies that schedule() and run() execute a task through the global dispatcher.
TEST_F(dispatcher_fixture, schedule_and_run) {
    ace::bus<int> channel;
    ace::schedule(push_value(channel, 42));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto result = fetch(channel);
    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(42, result[0]);
}

// Verifies that the dispatcher is empty after all scheduled work completes.
TEST_F(dispatcher_fixture, empty_after_run) {
    ace::schedule(simple_dispatched());
    ace::run();
    EXPECT_TRUE(ace::empty());
}

// Verifies that reload() can increase the configured runner count and still execute work.
TEST_F(dispatcher_fixture, reload_increase) {
    ace::cfg::g_config._runners_amount = 2;
    ace::reload();
    ace::schedule(simple_dispatched());
    ace::run();
    EXPECT_TRUE(ace::empty());
}

// Verifies that reload() can decrease the configured runner count and still execute work.
TEST_F(dispatcher_fixture, reload_decrease) {
    ace::cfg::g_config._runners_amount = 1;
    ace::reload();
    ace::schedule(simple_dispatched());
    ace::run();
    EXPECT_TRUE(ace::empty());
}

// Verifies that interrupt() enqueues the concrete e_break signal exactly once.
TEST_F(dispatcher_fixture, interrupt_signal) {
    ace::interrupt();
    std::unique_ptr<ace::core::signal_handler> signal;
    ASSERT_TRUE(ace::core::dispatcher::get_sig_pipe().pop(signal));
    EXPECT_NE(nullptr, dynamic_cast<ace::core::interruption_signal*>(signal.get()));
    EXPECT_TRUE(ace::core::dispatcher::get_sig_pipe().empty());
}

// Verifies that terminate() enqueues the concrete e_shutdown signal exactly once.
TEST_F(dispatcher_fixture, terminate_signal) {
    ace::terminate();
    std::unique_ptr<ace::core::signal_handler> signal;
    ASSERT_TRUE(ace::core::dispatcher::get_sig_pipe().pop(signal));
    EXPECT_NE(nullptr, dynamic_cast<ace::core::termination_signal*>(signal.get()));
    EXPECT_TRUE(ace::core::dispatcher::get_sig_pipe().empty());
}

// Verifies that reset_signal() drains a mixed batch and is idempotent on an empty pipe.
TEST_F(dispatcher_fixture, reset_signal_drains_all_pending_signals) {
    ace::interrupt();
    ace::terminate();
    ace::interrupt();
    ace::reset_signal();
    EXPECT_TRUE(ace::core::dispatcher::get_sig_pipe().empty());
    ace::reset_signal();
    EXPECT_TRUE(ace::core::dispatcher::get_sig_pipe().empty());
}

// Verifies that separate schedule()/run() cycles retain their observable order.
TEST_F(dispatcher_fixture, multiple_schedule_run) {
    ace::bus<int> channel;
    ace::schedule(push_value(channel, 1));
    ace::run();
    EXPECT_TRUE(ace::empty());

    ace::schedule(push_value(channel, 2));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto result = fetch(channel);
    ASSERT_EQ(2u, result.size());
    EXPECT_EQ(1, result[0]);
    EXPECT_EQ(2, result[1]);
}

// Verifies that automatic selection distributes equal initial load without index bias.
TEST_F(dispatcher_fixture, balanced_selection_uses_every_runner) {
    ace::cfg::g_config._runners_amount = 4;
    ASSERT_TRUE(ace::reload());
    std::vector<ace::core::runner*> runners(400);
    for (std::size_t task = 0; task < runners.size(); ++task)
        ace::schedule(report_runner(runners, task));

    ace::run();

    std::multiset<ace::core::runner*> counts(runners.begin(), runners.end());
    std::set<ace::core::runner*> distinct(runners.begin(), runners.end());
    ASSERT_EQ(4u, distinct.size());
    std::size_t minimum = runners.size();
    std::size_t maximum = 0;
    for (auto* runner : distinct) {
        minimum = std::min(minimum, counts.count(runner));
        maximum = std::max(maximum, counts.count(runner));
    }
    // Equal queued work should remain tightly balanced despite approximate sampling.
    EXPECT_LE(maximum - minimum, 1u);
}

// Verifies that the 64-runner tie path cannot systematically exclude high indices.
TEST_F(dispatcher_fixture, balanced_selection_scales_to_sixty_four_runners) {
    ace::cfg::g_config._runners_amount = 64;
    ASSERT_TRUE(ace::reload());
    std::vector<ace::core::runner*> runners(64);
    for (std::size_t task = 0; task < runners.size(); ++task)
        ace::schedule(report_runner(runners, task));

    ace::run();
    // One complete rotating-anchor cycle must visit every equally loaded runner.
    EXPECT_EQ(64u, std::set<ace::core::runner*>(runners.begin(), runners.end()).size());
}

// Verifies that automatic scheduling avoids a runner with a large reserved queue.
TEST_F(dispatcher_fixture, selection_avoids_explicitly_overloaded_runner) {
    ace::cfg::g_config._runners_amount = 4;
    ASSERT_TRUE(ace::reload());
    std::vector<ace::core::runner*> discovered(4);
    for (std::size_t task = 0; task < discovered.size(); ++task)
        ace::schedule(report_runner(discovered, task));
    ace::run();
    std::set<ace::core::runner*> distinct(discovered.begin(), discovered.end());
    ASSERT_EQ(4u, distinct.size());
    auto* overloaded = *distinct.begin();

    for (int task = 0; task < 32; ++task)
        ace::schedule(simple_dispatched(), overloaded);
    std::vector<ace::core::runner*> selected(16);
    for (std::size_t task = 0; task < selected.size(); ++task)
        ace::schedule(report_runner(selected, task));

    ace::run();
    // Every two-choice comparison containing the loaded runner has a lighter peer.
    EXPECT_EQ(selected.end(), std::find(selected.begin(), selected.end(), overloaded));
}

// Verifies that multiple producer threads can call schedule() without losing work.
TEST_F(dispatcher_fixture, concurrent_schedule_producers_preserve_every_task) {
    ace::cfg::g_config._runners_amount = 4;
    ASSERT_TRUE(ace::reload());
    std::atomic_size_t completed = 0;
    constexpr int producers = 4;
    constexpr int tasks_per_producer = 250;
    std::vector<std::jthread> producer_threads;
    for (int producer = 0; producer < producers; ++producer) {
        producer_threads.emplace_back([&completed] {
            for (int task = 0; task < tasks_per_producer; ++task)
                ace::schedule(increment(completed));
        });
    }
    producer_threads.clear();

    ace::run();
    EXPECT_EQ(static_cast<std::size_t>(producers * tasks_per_producer), completed.load());
}

// Verifies that schedule() publishes work while another thread is inside run().
TEST_F(dispatcher_fixture, concurrent_schedule_during_run_executes_before_quiescence) {
    ace::cfg::g_config._runners_amount = 4;
    ASSERT_TRUE(ace::reload());
    std::atomic_bool started = false;
    std::atomic_bool stop = false;
    std::atomic_size_t completed = 0;
    ace::schedule(keep_run_active(started, stop));
    std::jthread runtime([] { ace::run(); });
    while (not started.load(std::memory_order_acquire))
        std::this_thread::yield();

    constexpr int scheduled = 500;
    for (int task = 0; task < scheduled; ++task)
        ace::schedule(increment(completed));
    stop.store(true, std::memory_order_release);
    runtime.join();

    // The active sentinel prevents quiescence until every concurrent publication is visible.
    EXPECT_EQ(static_cast<std::size_t>(scheduled), completed.load());
}

// Verifies that zero-runner reload is rejected without corrupting the old dispatcher.
TEST_F(dispatcher_fixture, reload_zero_is_transactional) {
    ace::cfg::g_config._runners_amount = 2;
    ASSERT_TRUE(ace::reload());
    ace::cfg::g_config._runners_amount = 0;
    EXPECT_FALSE(ace::reload());

    std::vector<ace::core::runner*> runners(2);
    ace::schedule(report_runner(runners, 0));
    ace::schedule(report_runner(runners, 1));
    ace::run();
    // Both old runners remaining observable proves that rejection did not partially commit.
    EXPECT_EQ(2u, std::set<ace::core::runner*>(runners.begin(), runners.end()).size());
}

// Verifies that reload rejects pending work and preserves it for the old configuration.
TEST_F(dispatcher_fixture, reload_busy_is_transactional) {
    ace::cfg::g_config._runners_amount = 2;
    ASSERT_TRUE(ace::reload());
    ace::bus<int> completed;
    ace::schedule(push_value(completed, 7));
    ace::cfg::g_config._runners_amount = 4;
    EXPECT_FALSE(ace::reload());

    ace::run();
    const auto values = fetch(completed);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(7, values[0]);
}
