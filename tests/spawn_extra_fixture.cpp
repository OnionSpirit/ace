#include <chrono>

#include "environment.h"

#include <ace/futures/polling.h>
#include <ace/futures/post.h>
#include <ace/futures/roaming.h>
#include <ace/futures/spawn.h>
#include <ace/futures/timeout.h>

namespace {

struct spawn_extra_fixture : base_fixture {};

struct roaming_probe : ace::core::traits::future_traits<roaming_probe> {
    bool observed = false;
    IMPORT_FUTURE_ENV(roaming_probe)

    bool await_suspend(auto coroutine) {
        observed = coroutine.promise()._roaming;
        return false;
    }
    bool await_resume() const noexcept { return observed; }
};

ace::task push_marker(ace::bus<int>& channel, int marker) {
    channel << marker;
    co_return;
}

ace::task delayed_marker(ace::bus<int>& channel, int marker) {
    co_await ace::timeout(std::chrono::milliseconds(500));
    channel << marker;
}

ace::task spawn_and_join_driver(ace::bus<int>& channel) {
    auto handle = co_await ace::spawn(push_marker(channel, 42));
    while (not handle.done())
        co_await ace::timeout(std::chrono::milliseconds(1));
    channel << 1;
}

ace::task cancel_and_join_driver(ace::bus<int>& channel) {
    auto handle = co_await ace::spawn(delayed_marker(channel, 99));
    handle.cancel();
    const bool joined = co_await handle.join();
    channel << (joined ? 1 : 0);
}

ace::task handle_done_driver(ace::bus<int>& channel) {
    auto handle = co_await ace::spawn(push_marker(channel, 7));
    while (not handle.done())
        co_await ace::timeout(std::chrono::milliseconds(1));
    channel << 1;
}

ace::task spawn_returns_handle_driver(ace::bus<int>& channel) {
    auto handle = co_await ace::spawn(push_marker(channel, 1));
    while (not handle.done())
        co_await ace::timeout(std::chrono::milliseconds(1));
    channel << 2;
}

ace::task post_priority_driver(ace::bus<int>& channel) {
    co_await ace::spawn(push_marker(channel, 1));
    co_await ace::post(push_marker(channel, 2));
    co_await ace::timeout(std::chrono::milliseconds(10));
    channel << 3;
}

ace::task roaming_driver(ace::bus<int>& channel, bool enabled) {
    co_await ace::roaming(enabled);
    channel << static_cast<int>(co_await roaming_probe {});
}

ace::task polling_driver(const bool enabled) {
    co_await ace::polling(enabled);
    co_await ace::suspend {};
}

// Verifies that a spawned task completes and its handle becomes done.
TEST_F(spawn_extra_fixture, spawn_and_join) {
    ace::bus<int> channel;
    ace::schedule(spawn_and_join_driver(channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(2u, values.size());
    // The child marker must precede the driver's done marker.
    EXPECT_EQ(42, values[0]);
    EXPECT_EQ(1, values[1]);
}

// Verifies that joining a canceled spawned task reports unsuccessful completion.
TEST_F(spawn_extra_fixture, join_after_cancel) {
    ace::bus<int> channel;
    ace::schedule(cancel_and_join_driver(channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    // Zero records that cancel prevented the child from reaching co_return.
    EXPECT_EQ(0, values[0]);
}

// Verifies that async_handle::done becomes true after the child reaches co_return.
TEST_F(spawn_extra_fixture, handle_done) {
    ace::bus<int> channel;
    ace::schedule(handle_done_driver(channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(2u, values.size());
    EXPECT_EQ(7, values[0]);
    // This marker is emitted only after done() changes to true.
    EXPECT_EQ(1, values[1]);
}

// Verifies that spawn returns a usable handle without suspending its caller permanently.
TEST_F(spawn_extra_fixture, spawn_returns_handle) {
    ace::bus<int> channel;
    ace::schedule(spawn_returns_handle_driver(channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(2u, values.size());
}

// Verifies that post inserts its task ahead of a spawn task on the same runner.
TEST_F(spawn_extra_fixture, post_uses_attach_front) {
    ace::bus<int> channel;
    ace::schedule(post_priority_driver(channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(3u, values.size());
    // post uses attach_front, while spawn appends to the regular queue.
    EXPECT_EQ(2, values[0]);
    EXPECT_EQ(1, values[1]);
    EXPECT_EQ(3, values[2]);
}

// Verifies that enabling roaming is an immediately usable coroutine operation.
TEST_F(spawn_extra_fixture, roaming_true) {
    ace::bus<int> channel;
    ace::schedule(roaming_driver(channel, true));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
}

// Verifies that disabling roaming is an immediately usable coroutine operation.
TEST_F(spawn_extra_fixture, roaming_false) {
    ace::bus<int> channel;
    ace::schedule(roaming_driver(channel, false));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(0, values[0]);
}

// Verifies that polling=true moves a suspended task into the service pool.
TEST_F(spawn_extra_fixture, polling_true) {
    ace::core::runner runner;
    runner.attach(polling_driver(true));
    ASSERT_TRUE(runner.yank());
    EXPECT_TRUE(runner.is_polling());
    EXPECT_TRUE(runner.yank_service());
    EXPECT_TRUE(runner.empty());
}

// Verifies that polling=false keeps a suspended task in the normal task pool.
TEST_F(spawn_extra_fixture, polling_false) {
    ace::core::runner runner;
    runner.attach(polling_driver(false));
    ASSERT_TRUE(runner.yank());
    EXPECT_FALSE(runner.is_polling());
    // The insertion/local selector flips source on the first empty probe.
    EXPECT_FALSE(runner.yank());
    EXPECT_TRUE(runner.yank());
    EXPECT_TRUE(runner.empty());
}

} // namespace
