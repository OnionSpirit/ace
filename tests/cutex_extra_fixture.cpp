#include <stdexcept>

#include "environment.h"

#include <ace/futures/cutex.h>

namespace {

struct cutex_extra_fixture : base_fixture {
    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
        ace::reset_signal();
    }

    ace::cutex cutex;
};

ace::task report_try_lock(ace::cutex& cutex, ace::bus<bool>& result) {
    auto guard = ace::guard(cutex);
    auto capture = guard.capture();
    result << capture.await_ready();
    co_await guard.release();
}

ace::task verify_raii_release(ace::cutex& cutex, ace::bus<int>& result) {
    {
        auto first = ace::guard(cutex);
        co_await first.capture();
        result << 1;
    }
    auto second = ace::guard(cutex);
    co_await second.capture();
    result << 2;
    co_await second.release();
}

ace::task verify_explicit_reacquire(ace::cutex& cutex, ace::bus<int>& result) {
    auto guard = ace::guard(cutex);
    co_await guard.capture();
    result << 1;
    co_await guard.release();

    co_await guard.capture();
    result << 2;
    co_await guard.release();
}

// Verifies that capture's fast path reports ready for an unlocked cutex.
TEST_F(cutex_extra_fixture, try_lock_free) {
    ace::bus<bool> result;
    ace::schedule(report_try_lock(cutex, result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(1u, values.size());
    EXPECT_TRUE(values[0]);
}

// Verifies that a proxy rejects a second capture before release.
TEST_F(cutex_extra_fixture, proxy_double_capture) {
    ace::guard guard(cutex);
    auto capture = guard.capture();
    ASSERT_TRUE(capture.await_ready());
    EXPECT_THROW(static_cast<void>(guard.capture()), std::logic_error);
    auto release = guard.release();
    (void)release;
}

// Verifies that releasing an already released proxy is a no-op.
TEST_F(cutex_extra_fixture, proxy_double_sync) {
    auto guard = ace::guard(cutex);
    auto capture = guard.capture();
    ASSERT_TRUE(capture.await_ready());
    auto release = guard.release();
    (void)release;
    EXPECT_NO_THROW((void)guard.release());
}

// Verifies that proxy destruction releases a capture for the next acquirer.
TEST_F(cutex_extra_fixture, proxy_destructor_sync) {
    ace::bus<int> result;
    ace::schedule(verify_raii_release(cutex, result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(2u, values.size());
    // Reaching marker 2 proves the first proxy's destructor unlocked the cutex.
    EXPECT_EQ(1, values[0]);
    EXPECT_EQ(2, values[1]);
}

// Verifies that explicit release permits the same proxy to reacquire the cutex.
TEST_F(cutex_extra_fixture, explicit_release_allows_reacquire) {
    ace::bus<int> result;
    ace::schedule(verify_explicit_reacquire(cutex, result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(2u, values.size());
    // Ordered markers distinguish a real release/reacquire cycle from one capture.
    EXPECT_EQ(1, values[0]);
    EXPECT_EQ(2, values[1]);
}

} // namespace
