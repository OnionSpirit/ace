#include <ranges>
#include <gtest/gtest.h>
#include "environment.h"
#include "units.h"
#include "ace/core/runner.h"

TEST(context, do_co_await_test) {
    auto r = simple_context_test();
    ASSERT_FALSE(r);
    r.awake();
    ASSERT_TRUE(r);
}

// NOTE: Проверка разрешения не константных реакций
TEST(context, do_nested_suspend_test) {

    auto r = nested_context_suspender();
    ASSERT_FALSE(r);
    r.awake(); // NOTE: Делаем так потому что ASSERT_TRUE может вызвать только константный оператор
    r.awake();
    ASSERT_TRUE(r);
}

// NOTE: Разрешать константные реакции не по деструктору запрещено
TEST(context, do_const_nested_suspend_test) {

    const auto r = nested_context_suspender();
    ASSERT_FALSE(r);
    ASSERT_FALSE(r);
}

TEST(context, do_empty_context_test) {

    auto r = ace::coroutines::context<>();
    ASSERT_TRUE(r);
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
    dispatcher.spawn(timer_waiter_valued(500ms, _channel));
    dispatcher.spawn(timer_waiter_valued(450ms, _channel));
    dispatcher.spawn(timer_waiter_valued(400ms, _channel));
    dispatcher.spawn(timer_waiter_valued(399ms, _channel));
    dispatcher.spawn(timer_waiter_valued(350ms, _channel));
    dispatcher.spawn(timer_waiter_valued(300ms, _channel));
    dispatcher.spawn(timer_waiter_valued(250ms, _channel));
    dispatcher.spawn(timer_waiter_valued(200ms, _channel));
    dispatcher.spawn(timer_waiter_valued(150ms, _channel));
    dispatcher.spawn(timer_waiter_valued(100ms, _channel));
    dispatcher.spawn(timer_waiter_valued(50ms, _channel));
    dispatcher.spawn(timer_waiter_valued(10ms, _channel));
    dispatcher.run();
    ASSERT_TRUE(dispatcher.empty());

    // NOTE: Collecting waited time sequence
    std::vector<int> res{};
    dispatcher.spawn(channel_fetcher(_channel, res));
    dispatcher.run();
    ASSERT_TRUE(dispatcher.empty());

    // NOTE: Waited time sequence must monotonically increase. (Time is monotonic MA DUDES)
    // NOTE: This means that timers are processed according to the sequence of expiration timestamps
    // NOTE: without additional delay. This proves that the expiration sequence is ordered.
    for (std::size_t i = 1; i < res.size(); ++i)
        ASSERT_TRUE(res.at(i) >= res.at(i - 1));
}

TEST(futures, do_timer_on_runner_parallel_test) {
    auto start_time = std::chrono::_V2::high_resolution_clock::now();
    for (int i = 0; i < 1000000; ++i) {
        for (int q = 0; q < 500; q += 50)
            dispatcher.spawn(timer_waiter(std::chrono::milliseconds(q)));
    }
    std::cout << "Tasks spawned" << std::endl;
    dispatcher.run();
    auto end_time = std::chrono::_V2::high_resolution_clock::now();
    auto ms_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    ASSERT_TRUE(dispatcher.empty());
    // NOTE: Check for parallel processing
    ASSERT_TRUE(ms_time < 10000);
}

