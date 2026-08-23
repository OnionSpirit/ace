#include <vector>

#include <gtest/gtest.h>

#include <ace/ace.h>
#include <ace/futures/channel.h>

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

// Verifies that interrupt() can enqueue and reset an e_break signal safely.
TEST_F(dispatcher_fixture, interrupt_signal) {
    ace::interrupt();
    ace::reset_signal();
    SUCCEED();
}

// Verifies that terminate() can enqueue and reset an e_shutdown signal safely.
TEST_F(dispatcher_fixture, terminate_signal) {
    ace::terminate();
    ace::reset_signal();
    SUCCEED();
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
