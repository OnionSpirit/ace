#include <ranges>
#include <gtest/gtest.h>
#include "environment.h"

// ==========================================================================
// context — low-level coroutine tests
// ==========================================================================

TEST_F(ContextFixture, do_co_await_test) {
    auto r = simple_context_test();
    r._coroutine.promise()._runner = reinterpret_cast<ace::task::runner_pool_t*>(uintptr_t{1});
    ASSERT_TRUE(r);
    r.awake();
    ASSERT_FALSE(r);
}

TEST_F(ContextFixture, do_nested_suspend_test) {
    auto r = nested_context_suspender();
    r._coroutine.promise()._runner = reinterpret_cast<ace::task::runner_pool_t*>(uintptr_t{1});
    ASSERT_TRUE(r);
    r.awake();
    r.awake();
    ASSERT_FALSE(r);
}

TEST_F(ContextFixture, do_const_nested_suspend_test) {
    const auto r = nested_context_suspender();
    r._coroutine.promise()._runner = reinterpret_cast<ace::task::runner_pool_t*>(uintptr_t{1});
    ASSERT_TRUE(r);
    ASSERT_TRUE(r);
}

TEST_F(ContextFixture, do_empty_context_test) {
    auto r = ace::task();
    ASSERT_FALSE(r);
}

// ==========================================================================
// core — runner, or/and, fs tests
// ==========================================================================

TEST_F(ContextFixture, do_runner_test) {
    ace::core::runner runner;
    runner.attach(nested_context_suspender());
    ASSERT_TRUE(runner.run());
    ASSERT_TRUE(runner.empty());
}

TEST_F(TimerFixture, do_or_await_test) {
    const auto start_time = std::chrono::steady_clock::now();
    ace::schedule(timer_or_timer());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    EXPECT_GE(ms_time, 100);
    EXPECT_LT(ms_time, 500);
}

TEST_F(TimerFixture, do_or_with_promise_tests) {
    ace::schedule(or_with_async());
    ace::run();
    ASSERT_TRUE(ace::empty());
}

TEST_F(TimerFixture, do_and_await_test) {
    const auto start_time = std::chrono::steady_clock::now();
    ace::schedule(timer_and_timer());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    EXPECT_GE(ms_time, 95);
}

TEST_F(FsFixture, do_fs_tests) {
    ace::schedule(fs_testing());
    ace::run();
    ASSERT_TRUE(ace::empty());
}

// ==========================================================================
// futures — channel, timer, expire, cutex race, socket echo
// ==========================================================================

TEST_F(ChannelFixture, do_dynamic_channel_on_runner_test) {
    ace::schedule(channel_sender());
    ace::schedule(channel_receiver());
    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_TRUE(_channel.empty());
    ASSERT_TRUE(_channel._waiters.empty());
}

TEST_F(TimerFixture, do_timer_on_runner_test) {
    using namespace std::chrono_literals;
    ace::schedule(timer_waiter_valued(501ms, _int_channel));
    ace::schedule(timer_waiter_valued(500ms, _int_channel));
    ace::schedule(timer_waiter_valued(450ms, _int_channel));
    ace::schedule(timer_waiter_valued(401ms, _int_channel));
    ace::schedule(timer_waiter_valued(400ms, _int_channel));
    ace::schedule(timer_waiter_valued(399ms, _int_channel));
    ace::schedule(timer_waiter_valued(350ms, _int_channel));
    ace::schedule(timer_waiter_valued(300ms, _int_channel));
    ace::schedule(timer_waiter_valued(256ms, _int_channel));
    ace::schedule(timer_waiter_valued(250ms, _int_channel));
    ace::schedule(timer_waiter_valued(200ms, _int_channel));
    ace::schedule(timer_waiter_valued(150ms, _int_channel));
    ace::schedule(timer_waiter_valued(100ms, _int_channel));
    ace::schedule(timer_waiter_valued(50ms, _int_channel));
    ace::schedule(timer_waiter_valued(10ms, _int_channel));
    ace::schedule(timer_waiter_valued(0ms, _int_channel));
    ace::run();
    ASSERT_TRUE(ace::empty());

    auto res = fetch(_int_channel);
    for (std::size_t i = 1; i < res.size(); ++i)
        ASSERT_GE(res[i], res[i - 1]);
}

TEST_F(TimerFixture, do_expire_on_runner_test) {
    using namespace std::chrono_literals;
    const auto now = ace::services::clock::current_time();
    ace::schedule(expire_waiter_valued(now + 501ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 500ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 450ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 401ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 400ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 399ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 350ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 300ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 256ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 250ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 200ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 150ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 100ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 50ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 10ms, _tp_channel));
    ace::schedule(expire_waiter_valued(now + 0ms, _tp_channel));
    ace::run();
    ASSERT_TRUE(ace::empty());

    auto res = fetch(_tp_channel);
    for (std::size_t i = 1; i < res.size(); ++i)
        ASSERT_GE(res[i], res[i - 1]);
}

TEST_F(CutexFixture, cutex_race) {
    configure_runners(8);
    std::string shared_cnt {"0"};
    constexpr int max_ = 100000;
    for (volatile std::size_t i = 0; i < _runners; i = i + 1)
        ace::schedule(racer(max_, shared_cnt));
    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_EQ(std::stoi(shared_cnt), max_ * _runners);
}

TEST_F(CutexFixture, cutex_race_resheduling) {
    configure_runners(8);
    _cutex.set_rescheduling(true);
    std::string shared_cnt {"0"};
    constexpr int max_ = 1000000;
    for (volatile std::size_t i = 0; i < _runners; i = i + 1)
        ace::schedule(racer(max_, shared_cnt));
    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_EQ(std::stoi(shared_cnt), max_ * _runners);
}

TEST_F(TimerParallelFixture, do_timer_on_runner_parallel_test) {
    using namespace std::chrono_literals;
    constexpr long sets_count = 1000000;
    constexpr long max_in_set = 500;
    constexpr long set_step = 50;
    constexpr long set_size = max_in_set / set_step;

    for (int i = 0; i < sets_count; ++i)
        for (int q = 0; q < max_in_set; q += set_step)
            ace::schedule(timer_waiter(std::chrono::milliseconds(q), _channel));

    std::cout << "Tasks spawned" << std::endl;
    const auto start_time = std::chrono::steady_clock::now();
    ace::run();
    const auto end_time = std::chrono::steady_clock::now();
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    std::cout << "Timers released after: " << ms_time << "ms.\n\t"
                 "Timers amount: " << sets_count * set_size << ".\n\t"
                 "Durations range: [" << set_step << "ms, " << max_in_set
              << "ms], step: " << set_step << std::endl;
    ASSERT_TRUE(ace::empty());

    std::vector<long> res;
    ace::schedule(channel_fetcher(_channel, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_EQ(res.size(), set_size * sets_count);

    long real_sum {}, exp_sum {};
    for (int i = 0; i < sets_count; ++i)
        for (int q = 0; q < max_in_set; q += set_step)
            exp_sum += q;
    for (auto r : res) real_sum += r;
    EXPECT_GT(real_sum, exp_sum);
}

TEST_F(SocketEchoFixture, do_io_socket_echo) {
    ace::schedule(socket_listener());
    ace::schedule(socket_abuser());
    ace::run();
    ASSERT_TRUE(ace::empty());
}

TEST_F(SocketEchoFixture, do_io_socket_echo_zc) {
    ace::schedule(socket_listener_zc());
    ace::schedule(socket_abuser_zc());
    ace::run();
    ASSERT_TRUE(ace::empty());
}

// ==========================================================================
// commands — spawn, post, cancel, join, compose, cutex cancel
// ==========================================================================

TEST_F(SpawnFixture, check_spawn_command) {
    ace::schedule(spawner());
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(_runner_channel);
    ASSERT_EQ(res.size(), 2);
    ASSERT_NE(res[0], nullptr);
    ASSERT_NE(res[1], nullptr);
    ASSERT_EQ(res[0], res[1]);
}

TEST_F(SpawnFixture, check_spawn_post) {
    ace::schedule(imposter(_int_channel));
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 5);
    ASSERT_EQ(res[0], 3);
    ASSERT_EQ(res[1], 1);
    ASSERT_EQ(res[2], 4);
    ASSERT_EQ(res[3], 2);
    ASSERT_EQ(res[4], 5);
}

TEST_F(SpawnFixture, check_composed_output) {
    ace::schedule(composed_output(_int_channel));
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(_int_channel);
    ASSERT_EQ(res.size(), 5);
    ASSERT_EQ(res[0], 1);
    ASSERT_EQ(res[1], 2);
    ASSERT_EQ(res[2], 3);
    ASSERT_EQ(res[3], 4);
    ASSERT_EQ(res[4], 5);
}

TEST_F(SpawnFixture, check_spawn_and_join) {
    ace::schedule(join_spawner());
    ace::run();
    ASSERT_TRUE(ace::empty());
    auto res = fetch(_runner_channel);
    ASSERT_EQ(res.size(), 2);
    ASSERT_NE(res[0], nullptr);
    ASSERT_NE(res[1], nullptr);
    ASSERT_EQ(res[0], res[1]);
}

TEST_F(SpawnFixture, check_cancel) {
    const auto start_time = std::chrono::steady_clock::now();
    ace::schedule(spawner_cancel());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    auto res = fetch(_runner_channel);
    EXPECT_EQ(res.size(), 1);
    EXPECT_NE(res[0], nullptr);
    EXPECT_LT(ms_time, 900);
}

TEST_F(SpawnFixture, check_join_after_cancel) {
    const auto start_time = std::chrono::steady_clock::now();
    ace::schedule(spawner_join_canceled());
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    auto res = fetch(_runner_channel);
    EXPECT_EQ(res.size(), 1);
    EXPECT_NE(res[0], nullptr);
    EXPECT_LT(ms_time, 900);
}

TEST_F(CutexFixture, check_cutex_cancel_after_capture) {
    configure_runners(2);
    const auto start_time = std::chrono::steady_clock::now();

    ace::schedule(cutex_parallel());
    ace::schedule(cutex_spawner());
    ace::run();
    ASSERT_TRUE(ace::empty());

    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    auto res = fetch(_runner_channel);
    ASSERT_EQ(res.size(), 2);
    EXPECT_NE(res[0], nullptr);
    EXPECT_NE(res[1], nullptr);

    res.clear();
    ace::schedule(cutex_checker());
    ace::run();
    ASSERT_TRUE(ace::empty());
    res = fetch(_runner_channel);
    ASSERT_EQ(res.size(), 1);
    EXPECT_NE(res[0], nullptr);

    EXPECT_LT(ms_time, 900);
}

TEST_F(CutexFixture, check_cutex_cancel_before_capture) {
    configure_runners(2);
    const auto start_time = std::chrono::steady_clock::now();

    ace::schedule(cutex_parallel());
    ace::schedule(cutex_spawner_permanent());
    ace::run();
    ASSERT_TRUE(ace::empty());

    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    auto res = fetch(_runner_channel);
    EXPECT_EQ(res.size(), 2);
    EXPECT_NE(res[0], nullptr);
    EXPECT_NE(res[1], nullptr);

    res.clear();
    ace::schedule(cutex_checker());
    ace::run();
    ASSERT_TRUE(ace::empty());
    res = fetch(_runner_channel);
    EXPECT_EQ(res.size(), 1);
    EXPECT_NE(res[0], nullptr);

    EXPECT_LT(ms_time, 900);
}
