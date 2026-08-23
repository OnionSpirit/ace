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
        co_return;
    }

    ace::task channel_receiver() {
        const auto received = co_await _channel.pull();
        ace::println("Channel receive complete. DATA: {}", received);
        _channel << "Pong";
        ace::println("Channel send answer");
        co_return;
    }

    ace::bus<std::string> _channel {};
};

// Verifies bidirectional channel delivery and cleanup of all pending waiters.
TEST_F(channel_fixture, do_dynamic_channel_on_runner_test) {
    ace::schedule(channel_sender());
    ace::schedule(channel_receiver());
    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_TRUE(_channel.empty());
    // The waiter queue must drain as well as the data queue to avoid stale task nodes.
    ASSERT_TRUE(_channel._waiters.empty());
}

} // namespace
