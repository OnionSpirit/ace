#include <ranges>
#include <gtest/gtest.h>
#include "environment.h"

TEST(context, do_co_await_test) {
    auto r = simple_context_test();
    // NOTE: To let it be resumable
    r._coroutine.promise()._runner_pool = reinterpret_cast<ace::task::runner_pool_t*>(uintptr_t{1});
    ASSERT_TRUE(r);
    r.awake();
    ASSERT_FALSE(r);
}

TEST(context, do_nested_suspend_test) {

    auto r = nested_context_suspender();
    // NOTE: To let it be resumable
    r._coroutine.promise()._runner_pool = reinterpret_cast<ace::task::runner_pool_t*>(uintptr_t{1});
    ASSERT_TRUE(r);
    r.awake(); // NOTE: Делаем так потому что ASSERT_TRUE может вызвать только константный оператор
    r.awake();
    ASSERT_FALSE(r);
}

TEST(context, do_const_nested_suspend_test) {

    const auto r = nested_context_suspender();
    // NOTE: To let it be resumable
    r._coroutine.promise()._runner_pool = reinterpret_cast<ace::task::runner_pool_t*>(uintptr_t{1});
    ASSERT_TRUE(r);
    ASSERT_TRUE(r);
}

TEST(context, do_empty_context_test) {

    auto r = ace::task();
    ASSERT_FALSE(r);
}

TEST(core, do_runner_test) {

    ace::core::runner runner;
    runner.attach(nested_context_suspender());
    ASSERT_TRUE(runner.run());
    ASSERT_TRUE(runner.empty());
}

TEST(futures, do_dynamic_channel_on_runner_test) {
    channel_abuser abuser;
    ace::schedule(abuser.channel_receiver());
    ace::schedule(abuser.channel_sender());
    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_TRUE(abuser._channel.empty());
}

TEST(futures, do_timer_on_runner_test) {

    ace::futures::channel_dyn<int> _channel {};

    // NOTE: Spawning waiters with different duration and waited time count return
    ace::schedule(timer_waiter_valued(501ms, _channel));
    ace::schedule(timer_waiter_valued(500ms, _channel));
    ace::schedule(timer_waiter_valued(450ms, _channel));
    ace::schedule(timer_waiter_valued(401ms, _channel));
    ace::schedule(timer_waiter_valued(400ms, _channel));
    ace::schedule(timer_waiter_valued(399ms, _channel));
    ace::schedule(timer_waiter_valued(350ms, _channel));
    ace::schedule(timer_waiter_valued(300ms, _channel));
    ace::schedule(timer_waiter_valued(256ms, _channel));
    ace::schedule(timer_waiter_valued(250ms, _channel));
    ace::schedule(timer_waiter_valued(200ms, _channel));
    ace::schedule(timer_waiter_valued(150ms, _channel));
    ace::schedule(timer_waiter_valued(100ms, _channel));
    ace::schedule(timer_waiter_valued(50ms, _channel));
    ace::schedule(timer_waiter_valued(10ms, _channel));
    ace::schedule(timer_waiter_valued(0ms, _channel));
    ace::run();
    ASSERT_TRUE(ace::empty());

    // NOTE: Collecting waited time sequence
    std::vector<int> res{};
    ace::schedule(channel_fetcher(_channel, res));
    ace::run();
    ASSERT_TRUE(ace::empty());

    // NOTE: Waited time sequence must monotonically increase. (Time is monotonic MA DUDES)
    // NOTE: This means that timers are processed according to the sequence of expiration timestamps
    // NOTE: without additional delay. This proves that the expiration sequence is ordered.
    for (std::size_t i = 1; i < res.size(); ++i)
        ASSERT_GE(res[i], res[i - 1]);
}

TEST(futures, do_expire_on_runner_test) {
    ace::futures::channel_dyn<ace::core::timepoint_t> _channel {};

    const auto now = ace::core::modules::clock::current_time();
    // NOTE: Spawning waiters with different duration and waited time count return
    ace::schedule(expire_waiter_valued(now + 501ms, _channel));
    ace::schedule(expire_waiter_valued(now + 500ms, _channel));
    ace::schedule(expire_waiter_valued(now + 450ms, _channel));
    ace::schedule(expire_waiter_valued(now + 401ms, _channel));
    ace::schedule(expire_waiter_valued(now + 400ms, _channel));
    ace::schedule(expire_waiter_valued(now + 399ms, _channel));
    ace::schedule(expire_waiter_valued(now + 350ms, _channel));
    ace::schedule(expire_waiter_valued(now + 300ms, _channel));
    ace::schedule(expire_waiter_valued(now + 256ms, _channel));
    ace::schedule(expire_waiter_valued(now + 250ms, _channel));
    ace::schedule(expire_waiter_valued(now + 200ms, _channel));
    ace::schedule(expire_waiter_valued(now + 150ms, _channel));
    ace::schedule(expire_waiter_valued(now + 100ms, _channel));
    ace::schedule(expire_waiter_valued(now + 50ms, _channel));
    ace::schedule(expire_waiter_valued(now + 10ms, _channel));
    ace::schedule(expire_waiter_valued(now + 0ms, _channel));
    ace::run();
    ASSERT_TRUE(ace::empty());

    // NOTE: Collecting waited time sequence
    std::vector<ace::core::timepoint_t> res{};
    ace::schedule(channel_fetcher(_channel, res));
    ace::run();
    ASSERT_TRUE(ace::empty());

    // NOTE: Waited time sequence must monotonically increase. (Time is monotonic MA DUDES)
    // NOTE: This means that timers are processed according to the sequence of expiration timestamps
    // NOTE: without additional delay. This proves that the expiration sequence is ordered.
    for (std::size_t i = 1; i < res.size(); ++i)
        ASSERT_GE(res[i], res[i - 1]);
}

TEST(futures, cutex_race) {
    ace::core::s_dispatcher_config._runners_amount = 8;
    ace::reload();

    ace::cutex cutx_;

    std::string shared_cnt_ {"0"};
    constexpr int max_ = 100000;

    for (volatile std::size_t i = 0; i < ace::core::s_dispatcher_config._runners_amount; i = i + 1)
        ace::schedule(racer(max_, shared_cnt_, cutx_));

    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_EQ(std::stoi(shared_cnt_), max_ * ace::core::s_dispatcher_config._runners_amount);

    ace::core::s_dispatcher_config._runners_amount = 1;
    ace::reload();
    ace::reset_signal();
}

TEST(futures, cutex_race_resheduling) {
    ace::core::s_dispatcher_config._runners_amount = 8;
    ace::reload();

    ace::cutex cutx_;
    cutx_.set_rescheduling(true);
    // while (true) {
    std::string shared_cnt_ {"0"};
    constexpr int max_ = 1000000;

    for (volatile std::size_t i = 0; i < ace::core::s_dispatcher_config._runners_amount; i = i + 1)
        ace::schedule(racer(max_, shared_cnt_, cutx_));

    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_EQ(std::stoi(shared_cnt_), max_ * ace::core::s_dispatcher_config._runners_amount);
    // }

    ace::core::s_dispatcher_config._runners_amount = 1;
    ace::reload();
    ace::reset_signal();
}

TEST(futures, do_timer_on_runner_parallel_test) {
    ace::core::s_dispatcher_config._runners_amount = 4;
    ace::reload();

    ace::futures::channel_dyn<long> channel_ {};

    constexpr long sets_count = 1000000;
    constexpr long max_in_set = 500;
    constexpr long set_step = 50;
    constexpr long set_size = max_in_set / set_step;

    for (int i = 0; i < sets_count; ++i)
        for (int q = 0; q < max_in_set; q += set_step)
            ace::schedule(timer_waiter(std::chrono::milliseconds(q), channel_));

    std::cout << "Tasks spawned" << std::endl;
    const auto start_time = std::chrono::steady_clock::now();
    ace::run();
    const auto end_time = std::chrono::steady_clock::now();
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    std::cout << "Timers released after: " << ms_time << "ms.\n\t"
                 "Timers amount: " << sets_count * set_size << ".\n\t"
                 "Durations range: [" << set_step << "ms, " << max_in_set << "ms], step: " << set_step << std::endl;
    // NOTE: Check for parallel processing
    ASSERT_TRUE(ace::empty());

    // NOTE: Collecting waited time sequence
    std::vector<long> res{};
    ace::schedule(channel_fetcher(channel_, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    // NOTE: Check if all tasks sent response
    ASSERT_EQ(res.size(), set_size * sets_count);

    long real_sum {}, exp_sum {};
    // NOTE: expc_sum waited time in ms
    for (int i = 0; i < sets_count; ++i)
        for (int q = 0; q < max_in_set; q += set_step)
            exp_sum += q;
    // NOTE: real_sum waited time in ms
    for (auto r : res) real_sum += r;

    // NOTE: real_sum greater than exp_sum
    EXPECT_GT(real_sum, exp_sum);
    // NOTE: real_sum not greater than (exp_sum * 8 / thr_num) (Condition for <Ryzen 5 7500F, 64GB RAM DDR5>)
    // EXPECT_LT(real_sum, exp_sum * 8 / ace::core::s_dispatcher_config._runners_amount);

    ace::core::s_dispatcher_config._runners_amount = 1;
    ace::reload();
}

TEST(commands, check_spawn_command) {
    ace::futures::channel_dyn<ace::core::runner*> channel_ {};
    ace::schedule(spawner(channel_));
    ace::run();
    ASSERT_TRUE(ace::empty());
    // NOTE: Collecting waited time sequence
    std::vector<ace::core::runner*> res{};
    ace::schedule(channel_fetcher(channel_, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_EQ(res.size(), 2);
    ASSERT_NE(res[0], nullptr);
    ASSERT_NE(res[1], nullptr);
    ASSERT_EQ(res[0], res[1]);
}

TEST(commands, check_spawn_and_join) {
    ace::futures::channel_dyn<ace::core::runner*> channel_ {};
    ace::schedule(join_spawner(channel_));
    ace::run();
    ASSERT_TRUE(ace::empty());
    // NOTE: Collecting waited time sequence
    std::vector<ace::core::runner*> res{};
    ace::schedule(channel_fetcher(channel_, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_EQ(res.size(), 2);
    ASSERT_NE(res[0], nullptr);
    ASSERT_NE(res[1], nullptr);
    ASSERT_EQ(res[0], res[1]);
}

TEST(commands, check_cancel) {
    const auto start_time = std::chrono::steady_clock::now();
    ace::futures::channel_dyn<ace::core::runner*> channel_ {};
    ace::schedule(spawner_cancel(channel_));
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
    std::vector<ace::core::runner*> res{};
    ace::schedule(channel_fetcher(channel_, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    EXPECT_EQ(res.size(), 1);
    EXPECT_NE(res[0], nullptr);
    EXPECT_LT(ms_time, 900);
}

TEST(commands, check_join_after_cancel) {
    const auto start_time = std::chrono::steady_clock::now();
    ace::futures::channel_dyn<ace::core::runner*> channel_ {};
    ace::schedule(spawner_join_canceled(channel_));
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
    std::vector<ace::core::runner*> res{};
    ace::schedule(channel_fetcher(channel_, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    EXPECT_EQ(res.size(), 1);
    EXPECT_NE(res[0], nullptr);
    EXPECT_LT(ms_time, 900);
}


TEST(commands, check_cutex_cancel_after_capture) {
    ace::core::s_dispatcher_config._runners_amount = 2;
    ace::reload();

    const auto start_time = std::chrono::steady_clock::now();

    // NOTE: Scheduling spawner-canceler and parallel cutex user
    ace::futures::channel_dyn<ace::core::runner*> channel_ {};
    ace::cutex cutx_;
    ace::schedule(cutex_parallel(channel_, cutx_));
    ace::schedule(cutex_spawner(channel_, cutx_));
    ace::run();
    ASSERT_TRUE(ace::empty());

    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();

    // NOTE: Fetching data from channel. Each task produces 1 record in channel. The 'cutex_spawner' spawns extra task.
    // NOTE: If all tasks finished channel will handle 3 records but if we successfully canceled task then 2 records remain
    std::vector<ace::core::runner*> res{};
    ace::schedule(channel_fetcher(channel_, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    EXPECT_EQ(res.size(), 2);
    EXPECT_NE(res[0], nullptr);
    EXPECT_NE(res[1], nullptr);

    // NOTE: Trying to capture cutex to check if it is free after canceling owner task
    res.clear();
    ace::schedule(cutex_checker(channel_, cutx_));
    ace::run();
    ASSERT_TRUE(ace::empty());
    ace::schedule(channel_fetcher(channel_, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    EXPECT_EQ(res.size(), 1);
    EXPECT_NE(res[0], nullptr);

    EXPECT_LT(ms_time, 900);

    ace::core::s_dispatcher_config._runners_amount = 1;
    ace::reload();
}


TEST(commands, check_cutex_cancel_before_capture) {
    ace::core::s_dispatcher_config._runners_amount = 2;
    ace::reload();

    const auto start_time = std::chrono::steady_clock::now();

    // NOTE: Scheduling spawner-canceler and parallel cutex user
    ace::futures::channel_dyn<ace::core::runner*> channel_ {};
    ace::cutex cutx_;
    ace::schedule(cutex_parallel(channel_, cutx_));
    ace::schedule(cutex_spawner_permanent(channel_, cutx_));
    ace::run();
    ASSERT_TRUE(ace::empty());

    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();

    // NOTE: Fetching data from channel. Each task produces 1 record in channel. The 'cutex_spawner' spawns extra task.
    // NOTE: If all tasks finished channel will handle 3 records but if we successfully canceled task then 2 records remain
    std::vector<ace::core::runner*> res{};
    ace::schedule(channel_fetcher(channel_, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    EXPECT_EQ(res.size(), 2);
    EXPECT_NE(res[0], nullptr);
    EXPECT_NE(res[1], nullptr);

    // NOTE: Trying to capture cutex to check if it is free after canceling owner task
    res.clear();
    ace::schedule(cutex_checker(channel_, cutx_));
    ace::run();
    ASSERT_TRUE(ace::empty());
    ace::schedule(channel_fetcher(channel_, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    EXPECT_EQ(res.size(), 1);
    EXPECT_NE(res[0], nullptr);

    EXPECT_LT(ms_time, 900);

    ace::core::s_dispatcher_config._runners_amount = 1;
    ace::reload();
}

TEST(futures, do_io_socket_echo) {
    ace::schedule(socket_listener());
    ace::schedule(socket_abuser());
    ace::run();
    ASSERT_TRUE(ace::empty());
}

TEST(core, do_or_await_test) {
    const auto start_time = std::chrono::steady_clock::now();

    ace::schedule(timer_or_timer());
    ace::run();
    ASSERT_TRUE(ace::empty());

    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
    EXPECT_GE(ms_time, 100);
    EXPECT_LT(ms_time, 500);
}

TEST(core, do_and_await_test) {
    const auto start_time = std::chrono::steady_clock::now();

    ace::schedule(timer_and_timer());
    ace::run();
    ASSERT_TRUE(ace::empty());

    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
    EXPECT_GE(ms_time, 100);
}
