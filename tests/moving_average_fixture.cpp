#include <utility>

#include <gtest/gtest.h>

#include <ace/core/tools/moving_average.h>

namespace tool = ace::core::tools;

struct moving_average_fixture : ::testing::Test {};

// Verifies the average while the four-sample window is being filled.
TEST_F(moving_average_fixture, moving_average_basic) {
    tool::moving_average average;

    // The implementation includes initial zero slots until the window fills.
    EXPECT_EQ(5, average.add(10));
    EXPECT_EQ(10, average.add(20));
    EXPECT_EQ(15, average.add(30));
    EXPECT_EQ(25, average.add(40));
}

// Verifies that an untouched moving average reports zero.
TEST_F(moving_average_fixture, moving_average_zero) {
    tool::moving_average average;
    EXPECT_EQ(0, average.value());
}

// Verifies that new samples evict the oldest values after the window fills.
TEST_F(moving_average_fixture, moving_average_window) {
    tool::moving_average average;
    EXPECT_EQ(5, average.add(10));
    EXPECT_EQ(10, average.add(20));
    EXPECT_EQ(15, average.add(30));
    EXPECT_EQ(25, average.add(40));
    EXPECT_EQ(35, average.add(50));
    EXPECT_EQ(45, average.add(60));
}

// Verifies convergence and stability for a constant input value.
TEST_F(moving_average_fixture, moving_average_stability) {
    tool::moving_average average;
    EXPECT_EQ(50, average.add(100));
    EXPECT_EQ(66, average.add(100));
    EXPECT_EQ(75, average.add(100));
    EXPECT_EQ(100, average.add(100));

    // Once every slot contains 100, each replacement must preserve the mean.
    for (int i = 0; i < 6; ++i)
        EXPECT_EQ(100, average.add(100));
}

// Verifies that clear removes all accumulated samples.
TEST_F(moving_average_fixture, moving_average_clear) {
    tool::moving_average average;
    static_cast<void>(average.add(100));
    static_cast<void>(average.add(100));
    static_cast<void>(average.add(100));
    static_cast<void>(average.add(100));
    EXPECT_EQ(100, average.value());

    average.clear();
    EXPECT_EQ(0, average.value());
}

// Verifies that copying preserves the complete averaging state.
TEST_F(moving_average_fixture, moving_average_copy) {
    tool::moving_average source;
    static_cast<void>(source.add(10));
    static_cast<void>(source.add(20));
    static_cast<void>(source.add(30));
    static_cast<void>(source.add(40));
    tool::moving_average copy(source);

    EXPECT_EQ(source.value(), copy.value());
}

// Verifies that moving transfers state and resets the source.
TEST_F(moving_average_fixture, moving_average_move) {
    tool::moving_average source;
    static_cast<void>(source.add(10));
    static_cast<void>(source.add(20));
    static_cast<void>(source.add(30));
    static_cast<void>(source.add(40));
    const auto expected = source.value();
    tool::moving_average destination(std::move(source));

    EXPECT_EQ(expected, destination.value());
    EXPECT_EQ(0, source.value());
}
