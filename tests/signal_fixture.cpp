#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <ace/core/dispatcher.h>
#include <ace/core/signal.h>
#include <ace/futures/channel.h>

struct signal_fixture : ::testing::Test {
    using int_bus = ace::futures::channel<
        int,
        ace::futures::allocation_type::e_dynamic,
        ace::futures::access_mode::e_mpmc,
        0ul
    >;

    static ace::task run_termination_action(int_bus& channel) {
        ace::core::termination_signal signal;
        const auto result = co_await signal.action();
        channel << static_cast<int>(result);
        co_return;
    }

    static ace::task run_interruption_action(int_bus& channel) {
        ace::core::interruption_signal signal;
        const auto result = co_await signal.action();
        channel << static_cast<int>(result);
        co_return;
    }

    static ace::task drain_channel(int_bus& channel, std::vector<int>& output) {
        std::vector<int> values;
        while (not channel.empty())
            values.emplace_back(co_await channel.pull());
        output = std::move(values);
        co_return;
    }

    static std::vector<int> fetch(int_bus& channel) {
        std::vector<int> values;
        ace::schedule(drain_channel(channel, values));
        ace::run();
        EXPECT_TRUE(ace::empty());
        return values;
    }
};

// Verifies that termination_signal::action returns the shutdown order.
TEST_F(signal_fixture, termination_signal_action) {
    int_bus channel;
    ace::schedule(run_termination_action(channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto results = fetch(channel);
    ASSERT_GE(results.size(), 1u);
    EXPECT_EQ(static_cast<int>(ace::core::e_shutdown), results[0]);
}

// Verifies that interruption_signal::action returns the break order.
TEST_F(signal_fixture, interruption_signal_action) {
    int_bus channel;
    ace::schedule(run_interruption_action(channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto results = fetch(channel);
    ASSERT_GE(results.size(), 1u);
    EXPECT_EQ(static_cast<int>(ace::core::e_break), results[0]);
}

// Verifies that the signal pipe transfers ownership of a pushed handler.
TEST_F(signal_fixture, sig_pipe_push_pop) {
    ace::core::sig_pipe_t pipe;
    auto signal = ace::core::make_signal(ace::core::termination_signal {});
    pipe.push(std::move(signal));

    std::unique_ptr<ace::core::signal_handler> popped;
    EXPECT_TRUE(pipe.pop(popped));
    EXPECT_NE(nullptr, popped);
}

// Verifies that popping an empty signal pipe fails without producing a handler.
TEST_F(signal_fixture, sig_pipe_empty) {
    ace::core::sig_pipe_t pipe;
    std::unique_ptr<ace::core::signal_handler> signal;

    EXPECT_FALSE(pipe.pop(signal));
    EXPECT_EQ(nullptr, signal);
}
