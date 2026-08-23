#include <chrono>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <ace/ace.h>
#include <ace/core/runner.h>
#include <ace/futures/channel.h>
#include <ace/futures/timeout.h>

struct runner_fixture : ::testing::Test {
    static ace::task dummy_task() {
        co_return;
    }

    static ace::task suspending_task(ace::bus<int>& channel) {
        co_await ace::timeout(std::chrono::milliseconds(1));
        channel << 1;
    }

    static ace::task drain_channel(ace::bus<int>& channel, std::vector<int>& output) {
        while (not channel.empty())
            output.emplace_back(co_await channel.pull());
    }

    static std::vector<int> fetch(ace::bus<int>& channel) {
        std::vector<int> output;
        ace::schedule(drain_channel(channel, output));
        ace::run();
        EXPECT_TRUE(ace::empty());
        return output;
    }
};

// Verifies that attach() queues a task and run() executes it.
TEST_F(runner_fixture, attach_and_run) {
    ace::core::runner runner;
    runner.attach(dummy_task());
    EXPECT_FALSE(runner.empty());
    EXPECT_TRUE(runner.run());
    EXPECT_TRUE(runner.empty());
}

// Verifies that a newly constructed runner reports all task pools as empty.
TEST_F(runner_fixture, empty_all_pools) {
    ace::core::runner runner;
    EXPECT_TRUE(runner.empty());
}

// Verifies that queued work makes empty() false until the task is processed.
TEST_F(runner_fixture, empty_with_tasks) {
    ace::core::runner runner;
    runner.attach(dummy_task());
    EXPECT_FALSE(runner.empty());
    runner.run();
    EXPECT_TRUE(runner.empty());
}

// Verifies that run() reports no progress when the runner has no work.
TEST_F(runner_fixture, run_returns_false_when_idle) {
    ace::core::runner runner;
    EXPECT_FALSE(runner.run());
}

// Verifies that moving a runner transfers its queued task to the destination.
TEST_F(runner_fixture, runner_move) {
    ace::core::runner source;
    source.attach(dummy_task());
    EXPECT_FALSE(source.empty());

    ace::core::runner destination(std::move(source));
    EXPECT_FALSE(destination.empty());
    EXPECT_TRUE(destination.run());
    EXPECT_TRUE(destination.empty());
}

// Verifies that an idle runner has zero scheduling velocity.
TEST_F(runner_fixture, velocity_empty) {
    ace::core::runner runner;
    EXPECT_EQ(0.0, runner.velocity());
}

// Verifies that clear_velocity() leaves the scheduling metric at zero.
TEST_F(runner_fixture, clear_velocity) {
    ace::core::runner runner;
    runner.clear_velocity();
    EXPECT_EQ(0.0, runner.velocity());
}

// Verifies that a standalone runner preserves and resumes a timer-suspended task.
TEST_F(runner_fixture, suspending_task_run) {
    ace::bus<int> channel;
    ace::core::runner runner;
    runner.attach(suspending_task(channel));

    // The thread-local clock advances with real time, so pumping without a
    // delay could finish before the 1 ms timer becomes eligible.
    for (int i = 0; i < 10; ++i) {
        runner.run();
        if (not channel.empty())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto result = fetch(channel);
    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(1, result[0]);
}
