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
    dispatcher.spawn(timer_waiter(500ms));
    dispatcher.spawn(timer_waiter(200ms));
    dispatcher.spawn(timer_waiter(100ms));
    dispatcher.run();
    ASSERT_TRUE(dispatcher.empty());
}

TEST(futures, do_timer_on_runner_perf_test) {
    for (int i = 0; i < 1000000; ++i) {
        dispatcher.spawn(timer_waiter(500ms));
        dispatcher.spawn(timer_waiter(200ms));
        dispatcher.spawn(timer_waiter(100ms));
    }
    auto start_time = std::chrono::_V2::high_resolution_clock::now();
    dispatcher.run();
    auto end_time = std::chrono::_V2::high_resolution_clock::now();
    std::cout << "Timers await and processing duration without spawning period: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count()
        << "ms" << std::endl;
    ASSERT_TRUE(dispatcher.empty());
}

