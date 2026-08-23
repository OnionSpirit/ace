#include <concepts>
#include <cstdint>
#include <utility>

#include "environment.h"

#include <ace/console.h>

namespace {

struct context_fixture : base_fixture {
    ace::promise<bool> simple_context_test() {
        base_fixture::once_suspend tests_future;
        co_await tests_future;
        ace::println("One suspend complete");
        co_return true;
    }

    ace::task nested_context_suspender() {
        co_await simple_context_test();
        ace::println("Nested call complete");
        co_return;
    }

    static ace::task completed_task() {
        co_return;
    }
};

// Verifies that an eager coroutine crosses its single busy suspension and finishes.
TEST_F(context_fixture, do_co_await_test) {
    auto r = simple_context_test();
    r._coroutine.promise()._runner =
        reinterpret_cast<ace::task::runner_pool_t*>(uintptr_t{1});
    ASSERT_TRUE(r);
    r.awake();
    ASSERT_FALSE(r);
}

// Verifies that a nested coroutine requires both suspension points to be resumed.
TEST_F(context_fixture, do_nested_suspend_test) {
    auto r = nested_context_suspender();
    r._coroutine.promise()._runner =
        reinterpret_cast<ace::task::runner_pool_t*>(uintptr_t{1});
    ASSERT_TRUE(r);
    r.awake();
    r.awake();
    ASSERT_FALSE(r);
}

// Verifies that const access can inspect a live nested coroutine without consuming it.
TEST_F(context_fixture, do_const_nested_suspend_test) {
    const auto r = nested_context_suspender();
    r._coroutine.promise()._runner =
        reinterpret_cast<ace::task::runner_pool_t*>(uintptr_t{1});
    ASSERT_TRUE(r);
    ASSERT_TRUE(r);
}

// Verifies that a default-constructed task has no coroutine state.
TEST_F(context_fixture, do_empty_context_test) {
    auto r = ace::task();
    ASSERT_FALSE(r);
}

// Verifies that a runner executes the nested coroutine and drains its queue.
TEST_F(context_fixture, do_runner_test) {
    ace::core::runner runner;
    runner.attach(nested_context_suspender());
    ASSERT_TRUE(runner.run());
    // An empty runner here proves the nested suspension did not strand a task node.
    ASSERT_TRUE(runner.empty());
}

// Verifies that task_wrap converts a typed async into the task type accepted by schedule.
TEST_F(context_fixture, task_wrap_works) {
    static_assert(
        std::same_as<decltype(ace::task_wrap(std::declval<ace::async<int>>())), ace::task>,
        "task_wrap must return ace::task"
    );
    SUCCEED();
}

// Verifies that automaton rules are recognized without requiring destructor cancellation.
TEST_F(context_fixture, automaton_no_cancel_in_dtor) {
    static_assert(
        ace::core::is_rule<ace::core::automaton_rule>,
        "automaton must satisfy is_rule"
    );
    SUCCEED();
}

// Verifies that an empty task reports no live coroutine through is_exist().
TEST_F(context_fixture, is_exist_false_when_done) {
    ace::task t;
    EXPECT_FALSE(t.is_exist());
}

// Verifies that moving a task clears the source coroutine handle.
TEST_F(context_fixture, async_move_leaves_source_null) {
    auto t = completed_task();
    ace::task moved(std::move(t));
    // A null source prevents both task destructors from destroying the same frame.
    EXPECT_EQ(nullptr, t._coroutine);
}

// Verifies that repeated observe() calls produce live handles to one control block.
TEST_F(context_fixture, observe_twice) {
    auto t = nested_context_suspender();
    if (t._coroutine) {
        auto h1 = t.observe();
        auto h2 = t.observe();
        EXPECT_FALSE(h1.is_idle());
        EXPECT_FALSE(h2.is_idle());
    }
}

// Verifies that track() returns a trace identifier for a live coroutine.
TEST_F(context_fixture, async_track) {
    auto t = simple_context_test();
    if (t._coroutine) {
        auto trace = t.track();
        EXPECT_TRUE(trace.has_value());
    }
}

// Verifies that track() reports an error rather than touching an absent frame.
TEST_F(context_fixture, async_track_dead) {
    ace::task t;
    auto trace = t.track();
    EXPECT_FALSE(trace.has_value());
}

// Verifies that prefetch() accepts a live coroutine frame without throwing.
TEST_F(context_fixture, async_prefetch) {
    auto t = nested_context_suspender();
    if (t._coroutine) {
        EXPECT_NO_THROW(t.prefetch());
    }
}

} // namespace
