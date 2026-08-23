#include <chrono>
#include <cstddef>
#include <string>

#include "environment.h"

#include <ace/console.h>
#include <ace/core/tools/lifetime.h>
#include <ace/futures/cutex.h>
#include <ace/futures/get_runner.h>
#include <ace/futures/timeout.h>

using namespace std::chrono_literals;
namespace tool = ace::core::tools;

namespace {

struct cutex_fixture : base_fixture {
    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
        ace::reset_signal();
    }

    void configure_runners(int n) {
        _runners = n;
        ace::cfg::g_config._runners_amount = n;
        ace::reload();
    }

    ace::task capture_racer(const int max, std::string& counter) {
        ace::guard crx(_cutex);
        for (volatile int i = 0; i < max; i = i + 1) {
            co_await crx.capture();
            counter = std::to_string(std::stoi(counter) + 1);
            co_await crx.release();
            co_await crx.release();
        }
        co_await crx.capture();
        ace::println("'racer' finished");
    }

    ace::task sync_racer(const int max, std::string& counter) {
        ace::guard crx(_cutex);
        for (volatile int i = 0; i < max; i = i + 1) {
            co_await crx.sync();
            counter = std::to_string(std::stoi(counter) + 1);
            co_await crx.release();
            co_await crx.release();
        }
        co_await crx.capture();
        ace::println("'racer' finished");
    }

    ace::task cutex_parallel() {
        ace::println("'cutex_parallel' started");
        const auto wd = tool::lifetime("'cutex_parallel'");
        ace::guard crx(_cutex);
        co_await crx.capture();
        co_await ace::timeout(50ms);
        _runner_channel << co_await ace::get_runner();
        ace::println("{} finished", wd.mark());
    }

    ace::task cutex_carry() {
        ace::println("'cutex_carry' started");
        const auto wd = tool::lifetime("'cutex_carry'");
        ace::guard crx(_cutex);
        co_await crx.capture();
        ace::println("'cutex_carry' captured cutex");
        co_await ace::timeout(100ms);
        _runner_channel << co_await ace::get_runner();
        ace::println("{} finished", wd.mark());
    }

    ace::task cutex_checker() {
        ace::guard crx(_cutex);
        if (co_await (crx.capture() or ace::timeout(50ms)) == 0) {
            ace::println("'cutex_checker' captured cutex");
            _runner_channel << co_await ace::get_runner();
            ace::println("'cutex_checker' finished");
            co_return;
        }
        ace::println("'cutex_checker' can't capture cutex. FAILED");
    }

    ace::task cutex_spawner() {
        ace::println("'cutex_spawner' started");
        co_await ace::timeout(10ms);
        auto handle = co_await ace::spawn(cutex_carry());
        co_await ace::timeout(75ms);
        ace::println("'cutex_spawner' awake, canceling...");
        handle.cancel();
        co_await ace::timeout(10ms);
        if (not co_await handle.join())
            ace::println("'cutex_carry' canceled. Joining is 'false'");
        else
            ace::println("'cutex_carry' joined as alive. Failure");
        _runner_channel << co_await ace::get_runner();
        ace::println("'cutex_spawner' finished");
    }

    ace::task cutex_spawner_permanent() {
        ace::println("'cutex_spawner_permanent' started");
        co_await ace::timeout(10ms);
        auto handle = co_await ace::spawn(cutex_carry());
        co_await ace::timeout(25ms);
        ace::println("'cutex_spawner_permanent' awake, canceling...");
        handle.cancel();
        co_await ace::timeout(10ms);
        if (not co_await handle.join())
            ace::println("'cutex_carry' canceled. Joining is 'false'");
        else
            ace::println("'cutex_carry' joined as alive. Failure");
        _runner_channel << co_await ace::get_runner();
        ace::println("'cutex_spawner_permanent' finished");
    }

    ace::cutex _cutex {};
    ace::bus<ace::core::runner*> _runner_channel {};
    int _runners = 1;
};

// Verifies mutual exclusion under heavy capture contention on eight runners.
TEST_F(cutex_fixture, cutex_race) {
    configure_runners(8);
    std::string shared_cnt {"0"};
    constexpr int max_ = 10000;
    for (volatile std::size_t i = 0;
         i < static_cast<std::size_t>(_runners);
         i = i + 1)
        ace::schedule(capture_racer(max_, shared_cnt));
    ace::run();
    ASSERT_TRUE(ace::empty());
    // The exact total detects any lost or overlapping critical-section update.
    ASSERT_EQ(std::stoi(shared_cnt), max_ * _runners);
}

// Verifies mutual exclusion when sync() permits waiter migration between runners.
TEST_F(cutex_fixture, cutex_race_resheduling) {
    configure_runners(8);
    std::string shared_cnt {"0"};
    constexpr int max_ = 10000;
    for (volatile std::size_t i = 0;
         i < static_cast<std::size_t>(_runners);
         i = i + 1)
        ace::schedule(sync_racer(max_, shared_cnt));
    ace::run();
    ASSERT_TRUE(ace::empty());
    // The exact total proves rescheduling did not duplicate or drop ownership handoffs.
    ASSERT_EQ(std::stoi(shared_cnt), max_ * _runners);
}

// Verifies cancellation after cutex capture releases ownership for a later waiter.
TEST_F(cutex_fixture, check_cutex_cancel_after_capture) {
    configure_runners(2);
    const auto start_time = std::chrono::steady_clock::now();

    ace::schedule(cutex_parallel());
    ace::schedule(cutex_spawner());
    ace::run();
    ASSERT_TRUE(ace::empty());

    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    auto res = fetch(_runner_channel);
    ASSERT_EQ(res.size(), 2u);
    EXPECT_NE(res[0], nullptr);
    EXPECT_NE(res[1], nullptr);

    res.clear();
    ace::schedule(cutex_checker());
    ace::run();
    ASSERT_TRUE(ace::empty());
    res = fetch(_runner_channel);
    // A later successful capture proves cancellation released the cutex.
    ASSERT_EQ(res.size(), 1u);
    EXPECT_NE(res[0], nullptr);

    // Cancellation should avoid waiting for the canceled task's full timeout.
    EXPECT_LT(ms_time, 900);
}

// Verifies cancellation while waiting for cutex ownership leaves the cutex usable.
TEST_F(cutex_fixture, check_cutex_cancel_before_capture) {
    configure_runners(2);
    const auto start_time = std::chrono::steady_clock::now();

    ace::schedule(cutex_parallel());
    ace::schedule(cutex_spawner_permanent());
    ace::run();
    ASSERT_TRUE(ace::empty());

    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    auto res = fetch(_runner_channel);
    EXPECT_EQ(res.size(), 2u);
    EXPECT_NE(res[0], nullptr);
    EXPECT_NE(res[1], nullptr);

    res.clear();
    ace::schedule(cutex_checker());
    ace::run();
    ASSERT_TRUE(ace::empty());
    res = fetch(_runner_channel);
    // A later successful capture proves no canceled waiter remained at the queue head.
    EXPECT_EQ(res.size(), 1u);
    EXPECT_NE(res[0], nullptr);

    EXPECT_LT(ms_time, 900);
}

} // namespace
