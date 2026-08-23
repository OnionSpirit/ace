#include "environment.h"

namespace {

struct channel_extra_fixture : base_fixture {
    ace::bus<int> channel;
};

ace::task pull_one(ace::bus<int>& source, ace::bus<int>& result) {
    result << co_await source.pull();
}

ace::task push_three(ace::bus<int>& channel) {
    channel << 1;
    channel << 2;
    channel << 3;
    co_return;
}

ace::task pull_three(ace::bus<int>& source, ace::bus<int>& result) {
    result << co_await source.pull();
    result << co_await source.pull();
    result << co_await source.pull();
}

// Verifies that one pushed value is returned unchanged by pull().
TEST_F(channel_extra_fixture, push_pull_single) {
    channel.push(42);
    ASSERT_FALSE(channel.empty());
    ace::bus<int> result;
    ace::schedule(pull_one(channel, result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(42, values[0]);
}

// Verifies that operator<< has the same observable behavior as push().
TEST_F(channel_extra_fixture, operator_left_shift) {
    channel << 77;
    EXPECT_FALSE(channel.empty());
    ace::bus<int> result;
    ace::schedule(pull_one(channel, result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(77, values[0]);
}

// Verifies that empty() tracks the transition from no buffered value to one value.
TEST_F(channel_extra_fixture, channel_empty) {
    EXPECT_TRUE(channel.empty());
    channel << 10;
    EXPECT_FALSE(channel.empty());
}

// Verifies delivery of three values from a producer task to one consumer task.
TEST_F(channel_extra_fixture, mpsc_channel) {
    ace::bus<int> result;
    ace::schedule(push_three(channel));
    ace::schedule(pull_three(channel, result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(3u, values.size());
    // Summing is intentional: concurrent wake-up order is not part of this test.
    EXPECT_EQ(6, values[0] + values[1] + values[2]);
}

} // namespace
