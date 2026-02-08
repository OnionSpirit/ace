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
    runner.spawn(nested_context_suspender());
    runner.run();
    ASSERT_TRUE(runner.empty());
}

TEST(futures, do_dynamic_channel_on_runner_test) {

    ace::core::runner runner;
    channel_abuser abuser;
    runner.spawn(abuser.channel_receiver());
    runner.spawn(abuser.channel_sender());
    runner.run();
    ASSERT_TRUE(runner.empty());
    ASSERT_TRUE(abuser._channel.empty());
}

TEST(futures, do_timer_on_runner_test) {

    ace::futures::channel_dyn<int> _channel {};

    // NOTE: Spawning waiters with different duration and waited time count return
    ace::spawn(timer_waiter_valued(501ms, _channel));
    ace::spawn(timer_waiter_valued(500ms, _channel));
    ace::spawn(timer_waiter_valued(450ms, _channel));
    ace::spawn(timer_waiter_valued(401ms, _channel));
    ace::spawn(timer_waiter_valued(400ms, _channel));
    ace::spawn(timer_waiter_valued(399ms, _channel));
    ace::spawn(timer_waiter_valued(350ms, _channel));
    ace::spawn(timer_waiter_valued(300ms, _channel));
    ace::spawn(timer_waiter_valued(256ms, _channel));
    ace::spawn(timer_waiter_valued(250ms, _channel));
    ace::spawn(timer_waiter_valued(200ms, _channel));
    ace::spawn(timer_waiter_valued(150ms, _channel));
    ace::spawn(timer_waiter_valued(100ms, _channel));
    ace::spawn(timer_waiter_valued(50ms, _channel));
    ace::spawn(timer_waiter_valued(10ms, _channel));
    ace::spawn(timer_waiter_valued(0ms, _channel));
    ace::run();
    ASSERT_TRUE(ace::empty());

    // NOTE: Collecting waited time sequence
    std::vector<int> res{};
    ace::spawn(channel_fetcher(_channel, res));
    ace::run();
    ASSERT_TRUE(ace::empty());

    // NOTE: Waited time sequence must monotonically increase. (Time is monotonic MA DUDES)
    // NOTE: This means that timers are processed according to the sequence of expiration timestamps
    // NOTE: without additional delay. This proves that the expiration sequence is ordered.
    for (std::size_t i = 1; i < res.size(); ++i)
        ASSERT_TRUE(res.at(i) >= res.at(i - 1));
}

TEST(futures, do_timer_on_runner_parallel_test) {
    ace::core::s_balancer_config._runners_amount = 4;
    ace::core::clock::get_instance().enable_multithreading();
    ace::reload();

    for (int i = 0; i < 1000000; ++i) {
        for (int q = 0; q < 500; q += 50)
            ace::spawn(timer_waiter(std::chrono::milliseconds(q)));
    }
    std::cout << "Tasks spawned" << std::endl;
    auto start_time = std::chrono::_V2::high_resolution_clock::now();
    ace::run();
    auto end_time = std::chrono::_V2::high_resolution_clock::now();
    auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    std::cout << "Timers released after: " << ms_time << "ms" << std::endl;
    // NOTE: Check for parallel processing
    ASSERT_TRUE(ace::empty());

    ace::core::s_balancer_config._runners_amount = 1;
    ace::core::clock::get_instance().disable_multithreading();
    ace::reload();
}

TEST(futures, do_expire_on_runner_test) {
    ace::futures::channel_dyn<ace::core::timepoint_t> _channel {};

    auto now = ace::core::clock::current_time();
    // NOTE: Spawning waiters with different duration and waited time count return
    ace::spawn(expire_waiter_valued(now + 501ms, _channel));
    ace::spawn(expire_waiter_valued(now + 500ms, _channel));
    ace::spawn(expire_waiter_valued(now + 450ms, _channel));
    ace::spawn(expire_waiter_valued(now + 401ms, _channel));
    ace::spawn(expire_waiter_valued(now + 400ms, _channel));
    ace::spawn(expire_waiter_valued(now + 399ms, _channel));
    ace::spawn(expire_waiter_valued(now + 350ms, _channel));
    ace::spawn(expire_waiter_valued(now + 300ms, _channel));
    ace::spawn(expire_waiter_valued(now + 256ms, _channel));
    ace::spawn(expire_waiter_valued(now + 250ms, _channel));
    ace::spawn(expire_waiter_valued(now + 200ms, _channel));
    ace::spawn(expire_waiter_valued(now + 150ms, _channel));
    ace::spawn(expire_waiter_valued(now + 100ms, _channel));
    ace::spawn(expire_waiter_valued(now + 50ms, _channel));
    ace::spawn(expire_waiter_valued(now + 10ms, _channel));
    ace::spawn(expire_waiter_valued(now + 0ms, _channel));
    ace::run();
    ASSERT_TRUE(ace::empty());

    // NOTE: Collecting waited time sequence
    std::vector<ace::core::timepoint_t> res{};
    ace::spawn(channel_fetcher(_channel, res));
    ace::run();
    ASSERT_TRUE(ace::empty());

    // NOTE: Waited time sequence must monotonically increase. (Time is monotonic MA DUDES)
    // NOTE: This means that timers are processed according to the sequence of expiration timestamps
    // NOTE: without additional delay. This proves that the expiration sequence is ordered.
    for (std::size_t i = 1; i < res.size(); ++i)
        ASSERT_TRUE(res.at(i) >= res.at(i - 1));
}

