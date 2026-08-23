#include "environment.h"

#include <ace/futures/get_runner.h>

namespace {

struct get_runner_fixture : base_fixture {};

ace::task report_runner_presence(ace::bus<int>& result) {
    auto* runner = co_await ace::get_runner();
    result << (runner != nullptr ? 1 : 0);
}

// Verifies that get_runner returns the active runner inside a scheduled task.
TEST_F(get_runner_fixture, get_runner_inside_runner) {
    ace::bus<int> result;
    ace::schedule(report_runner_presence(result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
}

} // namespace
