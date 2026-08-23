#ifndef ACE_TESTS_ENVIRONMENT_H
#define ACE_TESTS_ENVIRONMENT_H

#include <utility>
#include <vector>

#include <gtest/gtest.h>

// Include ace.h before extension headers so the tests may use short aliases.
#include <ace/ace.h>
#include <ace/core/traits/future.h>
#include <ace/futures/channel.h>

/**
 * @brief Shared utilities used by multiple independent test fixtures.
 */
struct base_fixture : ::testing::Test {

    /**
     * @brief Busy future that suspends once and is ready on the next poll.
     */
    struct once_suspend : ace::core::traits::busy_future_traits<once_suspend> {
        IMPORT_BUSY_FUTURE_ENV(once_suspend)

        bool _trigger = false;

        bool await_ready() override {
            if (not _trigger) {
                _trigger = true;
                return false;
            }
            return true;
        }

        void await_suspend(auto) {}
        void await_resume() {}
    };

    /**
     * @brief Drains a channel into caller-owned storage.
     * @tparam T Channel value type.
     * @param channel Channel to drain.
     * @param output Receives values in pull order.
     */
    template <typename T>
    static ace::task channel_fetcher(
        ace::bus<T>& channel,
        std::vector<T>& output)
    {
        std::vector<T> values;
        while (not channel.empty())
            values.emplace_back(co_await channel.pull());
        output = std::move(values);
        co_return;
    }

    /**
     * @brief Synchronously drains a channel through the ACE dispatcher.
     * @tparam T Channel value type.
     * @param channel Channel to drain.
     * @return Values in pull order.
     */
    template <typename T>
    std::vector<T> fetch(ace::bus<T>& channel) {
        std::vector<T> values;
        ace::schedule(channel_fetcher(channel, values));
        ace::run();
        EXPECT_TRUE(ace::empty());
        return values;
    }
};

#endif // ACE_TESTS_ENVIRONMENT_H
