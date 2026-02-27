#include <ranges>
#include <gtest/gtest.h>
#include "environment.h"

TEST(context, do_co_await_test) {
    auto r = simple_context_test();
    ASSERT_TRUE(r);
    r.awake();
    ASSERT_FALSE(r);
}

// NOTE: Проверка разрешения не константных реакций
TEST(context, do_nested_suspend_test) {

    auto r = nested_context_suspender();
    ASSERT_TRUE(r);
    r.awake(); // NOTE: Делаем так потому что ASSERT_TRUE может вызвать только константный оператор
    r.awake();
    ASSERT_FALSE(r);
}

// NOTE: Разрешать константные реакции не по деструктору запрещено
TEST(context, do_const_nested_suspend_test) {

    const auto r = nested_context_suspender();
    ASSERT_TRUE(r);
    ASSERT_TRUE(r);
}

TEST(context, do_empty_context_test) {

    auto r = ace::coroutines::context<>();
    ASSERT_FALSE(r);
}

TEST(core, do_runner_test) {

    ace::core::runner runner;
    runner.attach(nested_context_suspender());
    ASSERT_FALSE(runner.run());
    ASSERT_TRUE(runner.empty());
}

TEST(futures, do_dynamic_channel_on_runner_test) {

    ace::core::runner runner;
    channel_abuser abuser;
    runner.attach(abuser.channel_receiver());
    runner.attach(abuser.channel_sender());
    ASSERT_FALSE(runner.run());
    ASSERT_TRUE(runner.empty());
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

    const auto now = ace::core::clock::current_time();
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
    ace::core::s_balancer_config._runners_amount = 12;
    ace::reload();

    ace::cutex cutx_;
    // while (true) {
    std::string shared_cnt_ {"0"};
    constexpr int max_ = 1000;

    for (volatile std::size_t i = 0; i < ace::core::s_balancer_config._runners_amount; i = i + 1)
        ace::schedule(racer(max_, shared_cnt_, cutx_));

    ace::run();
    ASSERT_TRUE(ace::empty());
    ASSERT_EQ(std::stoi(shared_cnt_), max_ * ace::core::s_balancer_config._runners_amount);
    // }

    ace::core::s_balancer_config._runners_amount = 1;
    ace::reload();
    ace::reset_signal();
}

TEST(futures, do_timer_on_runner_parallel_test) {
    ace::core::s_balancer_config._runners_amount = 4;
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
    const auto start_time = std::chrono::_V2::steady_clock::now();
    while (not ace::core::dispatcher::get_instance().empty())
        ace::run();
    const auto end_time = std::chrono::_V2::steady_clock::now();
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
    // EXPECT_LT(real_sum, exp_sum * 8 / ace::core::s_balancer_config._runners_amount);

    ace::core::s_balancer_config._runners_amount = 1;
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

TEST(commands, check_cancel) {
    const auto start_time = std::chrono::_V2::steady_clock::now();
    ace::futures::channel_dyn<ace::core::runner*> channel_ {};
    ace::schedule(spawner_cancel(channel_));
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::_V2::steady_clock::now() - start_time).count();
    // // NOTE: Collecting waited time sequence
    std::vector<ace::core::runner*> res{};
    ace::schedule(channel_fetcher(channel_, res));
    ace::run();
    ASSERT_TRUE(ace::empty());
    EXPECT_EQ(res.size(), 1);
    EXPECT_NE(res[0], nullptr);
    EXPECT_LT(ms_time, 900);
}

