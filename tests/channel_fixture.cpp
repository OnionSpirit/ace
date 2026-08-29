#include <string>

#include "environment.h"

#include <ace/console.h>

namespace {

struct channel_fixture : base_fixture {
    ace::task channel_sender() {
        base_fixture::once_suspend tests_future;
        co_await tests_future;
        std::string msg = "Ping";
        _channel.push(msg);
        ace::println("Channel send complete");
        co_await ace::suspend();
        const auto received = co_await _channel.pull();
        ace::println("Channel received answer. DATA: {}", received);
        sender_received = received;
        sender_done = true;
        co_return;
    }

    ace::task channel_receiver() {
        const auto received = co_await _channel.pull();
        ace::println("Channel receive complete. DATA: {}", received);
        receiver_received = received;
        _channel << "Pong";
        ace::println("Channel send answer");
        receiver_done = true;
        co_return;
    }

    ace::bus<std::string> _channel {};
    bool sender_done {};
    bool receiver_done {};
    std::string sender_received;
    std::string receiver_received;
};

// Verifies bidirectional channel delivery and cleanup of all pending waiters.
TEST_F(channel_fixture, do_dynamic_channel_on_runner_test) {
    ace::schedule(channel_sender());
    ace::schedule(channel_receiver());
    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_TRUE(_channel.empty());
    ASSERT_TRUE(sender_done);
    ASSERT_TRUE(receiver_done);
    ASSERT_EQ("Pong", sender_received);
    ASSERT_EQ("Ping", receiver_received);
    // Nukes empty() is intentionally weak and may report a false non-empty
    // snapshot; a failed pop directly proves that no stale waiter node remains.
    auto* const stale_waiter = _channel._waiters.pop_node();
    ASSERT_EQ(nullptr, stale_waiter);
}

} // namespace
