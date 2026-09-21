#include <new>
#include <thread>

#include "environment.h"
#include <ace/futures/timeout.h>

namespace {

using wheel = ace::services::hierarchical_time_wheel;

struct clock_initialization_fixture : ::testing::Test {
    void SetUp() override {
        ace::cfg::g_config._runners_amount = 1;
        ASSERT_TRUE(ace::reload());
    }

    void TearDown() override {
        EXPECT_TRUE(ace::empty());
        EXPECT_TRUE(ace::reload());
        ace::reset_signal();
    }
};

ace::task catch_initialization_and_retry(bool& caught, bool& completed) {
    try {
        co_await ace::timeout(std::chrono::milliseconds(1));
    } catch (const std::bad_alloc&) {
        caught = true;
    }
    wheel::set_initialization_for_testing(nullptr);
    co_await ace::timeout(std::chrono::milliseconds(1));
    completed = true;
}

void fail_level_storage(std::size_t position) {
    if (position == 0) throw std::bad_alloc {};
}

void fail_second_slot_storage(std::size_t position) {
    if (position == 2) throw std::bad_alloc {};
}

void check_fresh_thread_initialization(void (*failure)(std::size_t)) {
    bool caught = false;
    bool completed = false;
    // Each attempt needs untouched TLS, regardless of earlier shuffled tests.
    std::thread worker([&] {
        wheel::set_initialization_for_testing(failure);
        ace::schedule(catch_initialization_and_retry(caught, completed));
        ace::run();
    });
    worker.join();
    EXPECT_TRUE(caught);
    // Successful registration after the exception proves initialization retried.
    EXPECT_TRUE(completed);
    EXPECT_TRUE(ace::empty());
}

// Verifies first clock allocation failure reaches co_await and its next use retries.
TEST_F(clock_initialization_fixture, first_allocation_failure_rethrows_and_retries) {
    check_fresh_thread_initialization(fail_level_storage);
}

// Verifies partial wheel construction is destroyed and rebuilt before a timer retry.
TEST_F(clock_initialization_fixture, partial_initialization_failure_rethrows_and_retries) {
    check_fresh_thread_initialization(fail_second_slot_storage);
}

} // namespace
