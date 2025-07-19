#include <gtest/gtest.h>
#include "environment.h"
#include "units.h"
#include "include/ace/promises/react.h"

TEST(react, do_co_await_test) {
    auto r = simple_test();
    ASSERT_FALSE(r);
}

// NOTE: Проверка разрешения не константных реакций
TEST(react, do_nested_suspend_test) {

    auto r = nested_suspender();
    ASSERT_FALSE(r);
    r.resolve(); // NOTE: Делаем так потому что ASSERT_TRUE может вызвать только константный оператор
    ASSERT_TRUE(r);
}

// NOTE: Разрешать константные реакции не по деструктору запрещено
TEST(react, do_const_nested_suspend_test) {

    const auto r = nested_suspender();
    ASSERT_FALSE(r);
    ASSERT_FALSE(r);
}

TEST(react, do_empty_react_test) {

    auto r = ace::async::react<>();
    ASSERT_TRUE(r);
}

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

TEST(context, do_empty_react_test) {

    auto r = ace::async::context<>();
    ASSERT_TRUE(r);
}
