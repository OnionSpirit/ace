#include "environment.h"

#include <algorithm>

namespace {

struct channel_extra_fixture : base_fixture {
    ace::bus<int> channel;

    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ASSERT_TRUE(ace::reload());
    }
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

ace::task push_range(ace::bus<int>& target, int first, int count) {
    for (int offset = 0; offset < count; ++offset)
        target << first + offset;
    co_return;
}

ace::task pull_range(ace::bus<int>& source, ace::bus<int>& result, int count) {
    for (int index = 0; index < count; ++index)
        result << co_await source.pull();
    co_return;
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

// Verifies dynamic channel delivery with multiple producers and consumers on four runners.
TEST_F(channel_extra_fixture, channel_mpmc) {
    constexpr int participant_count = 4;
    constexpr int values_per_producer = 1000;
    constexpr int value_count = participant_count * values_per_producer;
    ace::cfg::g_config._runners_amount = participant_count;
    ASSERT_TRUE(ace::reload());

    ace::bus<int> result;
    for (int consumer = 0; consumer < participant_count; ++consumer)
        ace::schedule(pull_range(channel, result, values_per_producer));
    for (int producer = 0; producer < participant_count; ++producer)
        ace::schedule(push_range(channel, producer * values_per_producer, values_per_producer));
    ace::run();

    auto values = fetch(result);
    ASSERT_EQ(value_count, static_cast<int>(values.size()));
    std::ranges::sort(values);
    for (int expected = 0; expected < value_count; ++expected)
        EXPECT_EQ(expected, values[expected]);
    EXPECT_TRUE(channel.empty());
    EXPECT_EQ(nullptr, channel._waiters.pop_node());
}

} // namespace
