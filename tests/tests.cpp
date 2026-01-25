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

TEST(context, do_runner_test) {

    ace::core::runner runner;
    runner.spawn(nested_context_suspender());
    runner.run();
    ASSERT_TRUE(runner.empty());
}

TEST(context, do_dynamic_channel_on_runner_test) {

    ace::core::runner runner;
    channel_abuser abuser;
    runner.spawn(abuser.channel_receiver());
    runner.spawn(abuser.channel_sender());
    runner.run();
    ASSERT_TRUE(runner.empty());
    ASSERT_TRUE(abuser._channel.empty());
}

TEST(context, do_timer_on_runner_test) {
    dispatcher.spawn(timer_waiter());
    dispatcher.run();
    ASSERT_TRUE(dispatcher.empty());
}

