#include <chrono>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <ace/ace.h>
#include <ace/core/runner.h>
#include <ace/futures/channel.h>
#include <ace/futures/timeout.h>

struct runner_fixture : ::testing::Test {
    static ace::task dummy_task() {
        co_return;
    }

    static ace::task suspending_task(ace::bus<int>& channel) {
        co_await ace::timeout(std::chrono::milliseconds(1));
        channel << 1;
    }

    static ace::task drain_channel(ace::bus<int>& channel, std::vector<int>& output) {
        while (not channel.empty())
            output.emplace_back(co_await channel.pull());
    }

    static std::vector<int> fetch(ace::bus<int>& channel) {
        std::vector<int> output;
        ace::schedule(drain_channel(channel, output));
        ace::run();
        EXPECT_TRUE(ace::empty());
        return output;
    }
};

// Verifies that attach() queues a task and run() executes it.
TEST_F(runner_fixture, attach_and_run) {
    ace::core::runner runner;
    runner.attach(dummy_task());
    EXPECT_FALSE(runner.empty());
    EXPECT_TRUE(runner.run());
    EXPECT_TRUE(runner.empty());
}

// Verifies that a newly constructed runner reports all task pools as empty.
TEST_F(runner_fixture, empty_all_pools) {
    ace::core::runner runner;
    EXPECT_TRUE(runner.empty());
}

// Verifies that queued work makes empty() false until the task is processed.
TEST_F(runner_fixture, empty_with_tasks) {
    ace::core::runner runner;
    runner.attach(dummy_task());
    EXPECT_FALSE(runner.empty());
    runner.run();
    EXPECT_TRUE(runner.empty());
}

// Verifies that run() reports no progress when the runner has no work.
TEST_F(runner_fixture, run_returns_false_when_idle) {
    ace::core::runner runner;
    EXPECT_FALSE(runner.run());
}

// Verifies that moving a runner transfers its queued task to the destination.
TEST_F(runner_fixture, runner_move) {
    ace::core::runner source;
    source.attach(dummy_task());
    EXPECT_FALSE(source.empty());

    ace::core::runner destination(std::move(source));
    EXPECT_FALSE(destination.empty());
    EXPECT_TRUE(destination.run());
    EXPECT_TRUE(destination.empty());
}

// Verifies that a newly constructed runner publishes no runnable work.
TEST_F(runner_fixture, load_empty) {
    ace::core::runner runner;
    EXPECT_EQ(0u, runner.load());
}

// Verifies that attach publishes runnable load and terminal execution removes it.
TEST_F(runner_fixture, load_tracks_attach_completion) {
    ace::core::runner runner;
    runner.attach(dummy_task());
    EXPECT_EQ(1u, runner.load());
    EXPECT_TRUE(runner.run());
    EXPECT_EQ(0u, runner.load());
}

// Verifies that a standalone runner preserves and resumes a timer-suspended task.
TEST_F(runner_fixture, suspending_task_run) {
    ace::bus<int> channel;
    ace::core::runner runner;
    runner.attach(suspending_task(channel));

    // The thread-local clock advances with real time, so pumping without a
    // delay could finish before the 1 ms timer becomes eligible.
    for (int i = 0; i < 10; ++i) {
        runner.run();
        if (not channel.empty())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto result = fetch(channel);
    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(1, result[0]);
}

namespace {

// Observe the public load exactly between a declined redirect and publication.
struct declined_redirect : ace::core::traits::future_traits<declined_redirect> {
    IMPORT_FUTURE_ENV(declined_redirect)
    ace::core::runner& source;
    std::size_t& observed;
    declined_redirect(ace::core::runner& runner, std::size_t& load)
        : source(runner), observed(load) {}

    struct router final : ace::runner_router {
        ace::core::runner& source;
        std::size_t& observed;
        bool redirected = false;
        router(ace::core::runner& runner, std::size_t& load)
            : source(runner), observed(load) {}
        bool redirect(ace::omni_node) override { redirected = true; return false; }
        ~router() override {
            if (redirected)
                observed = source.load();
        }
    };
    bool await_suspend(auto coroutine) {
        coroutine.promise()._runner_router = router {source, observed};
        return true;
    }
    void await_resume() {}
};

ace::task observe_declined_redirect(
    ace::core::runner& runner, std::size_t& observed, bool& finished, bool polling)
{
    if (polling) {
        co_await ace::futures::polling(true);
        co_await ace::suspend {};
    }
    co_await declined_redirect(runner, observed);
    finished = true;
}

} // namespace

// Verifies that a declined regular redirect never exposes a false quiescent gap.
TEST_F(runner_fixture, declined_redirect_retains_source_load) {
    ace::core::runner runner;
    std::size_t observed = 0;
    bool finished = false;
    runner.attach(observe_declined_redirect(runner, observed, finished, false));
    // runner::run() is a bounded pumping round; dispatcher::run() is the drain API.
    while (not runner.quiescent())
        runner.run();
    EXPECT_TRUE(finished);
    // The router destructor runs before reattach acquires the destination count.
    EXPECT_EQ(1u, observed);
    EXPECT_EQ(0u, runner.load());
}

// Verifies that polling redirects use the same uninterrupted load accounting.
TEST_F(runner_fixture, polling_declined_redirect_retains_source_load) {
    ace::core::runner runner;
    std::size_t observed = 0;
    bool finished = false;
    runner.attach(observe_declined_redirect(runner, observed, finished, true));
    // runner::run() is a bounded pumping round; dispatcher::run() is the drain API.
    while (not runner.quiescent())
        runner.run();
    EXPECT_TRUE(finished);
    EXPECT_EQ(1u, observed);
    EXPECT_EQ(0u, runner.load());
}
