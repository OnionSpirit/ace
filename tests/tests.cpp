#include <gtest/gtest.h>
#include "environment.h"
#include "units.h"

TEST(context, do_co_await_test) {
    auto r = simple_context_test();
    ASSERT_FALSE(r);
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

    auto r = ace::promises::async<>();
    ASSERT_TRUE(r);
}
