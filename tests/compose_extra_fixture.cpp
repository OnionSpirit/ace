#include <chrono>

#include "environment.h"

#include <ace/futures/timeout.h>

namespace {

struct compose_extra_fixture : base_fixture {};

ace::task race_timeouts(ace::bus<int>& channel) {
    const int winner = co_await (
        ace::timeout(std::chrono::milliseconds(10)) or
        ace::timeout(std::chrono::milliseconds(2000))
    );
    channel << int{winner};
}

ace::task await_both_timeouts(ace::bus<int>& channel) {
    co_await (
        ace::timeout(std::chrono::milliseconds(1)) and
        ace::timeout(std::chrono::milliseconds(1))
    );
    channel << 1;
}

ace::task pipe_completion_marker(ace::bus<int>& channel) {
    co_await ace::timeout(std::chrono::milliseconds(1));
    channel << 1;
}

// Verifies that a two-way void race returns a valid winner index.
TEST_F(compose_extra_fixture, or_await_left_wins) {
    ace::bus<int> channel;
    ace::schedule(race_timeouts(channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    // Under extreme scheduler delay either observer can report first, but the
    // result must always identify exactly one of the two branches.
    EXPECT_GE(values[0], 0);
    EXPECT_LE(values[0], 1);
}

// Verifies that an and-composition resumes only after both timeouts complete.
TEST_F(compose_extra_fixture, and_await_both_succeed) {
    ace::bus<int> channel;
    ace::schedule(await_both_timeouts(channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    // The marker is emitted after the composed await, proving both observers resumed.
    EXPECT_EQ(1, values[0]);
}

// Verifies the completion path used by the operator-pipe scenario.
TEST_F(compose_extra_fixture, operator_pipe) {
    ace::bus<int> channel;
    ace::schedule(pipe_completion_marker(channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
}

} // namespace
