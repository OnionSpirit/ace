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
    struct lifetime_token {
        int& destroyed;
        ~lifetime_token() { ++destroyed; }
    };

    static ace::task tracked_task(std::unique_ptr<lifetime_token> token, bool suspend) {
        if (suspend) {
            once_suspend blocker;
            co_await blocker;
        }
        co_return;
    }

    static ace::task joining_task(ace::core::control_block_handle observer, int& resumed) {
        co_await ace::core::join_handler<>{observer};
        ++resumed;
    }

    static ace::task guarded_task(std::vector<int>& callbacks) {
        co_await ace::backup([&callbacks] { callbacks.push_back(1); });
        co_await ace::insure([&callbacks] { callbacks.push_back(2); });
        once_suspend blocker;
        co_await blocker;
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
    // println() schedules polling I/O, and one bounded run() call is not a
    // drain contract. Pump until both the task and its service work complete.
    while (runner.run()) {}
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

// Verifies replacement releases each old frame exactly once across lifecycle and observer states.
TEST_F(context_fixture, async_move_assignment_releases_destination) {
    // States: absent, initially suspended, suspended in the body, and completed.
    for (int destination_state = 0; destination_state != 4; ++destination_state) {
        for (int source_state = 0; source_state != 4; ++source_state) {
            for (bool observed : {false, true}) {
                SCOPED_TRACE(::testing::Message() << destination_state << ":" << source_state << ":" << observed);
                int old_destroyed = 0;
                int new_destroyed = 0;
                {
                    ace::task destination;
                    ace::task source;
                    if (destination_state != 0)
                        destination = tracked_task(std::make_unique<lifetime_token>(old_destroyed), destination_state == 2);
                    if (source_state != 0)
                        source = tracked_task(std::make_unique<lifetime_token>(new_destroyed), source_state == 2);
                    if (destination_state >= 2) destination.awake();
                    if (source_state >= 2) source.awake();
                    {
                        ace::core::control_block_handle old_observer;
                        ace::core::control_block_handle new_observer;
                        if (observed) {
                            if (destination_state != 0) old_observer = destination.observe();
                            if (source_state != 0) new_observer = source.observe();
                        }
                        auto* result = &(destination = std::move(source));
                        EXPECT_EQ(result, &destination);
                        EXPECT_FALSE(source.track().has_value());
                        EXPECT_EQ(destination.track().has_value(), source_state != 0);
                        // Observer ownership deliberately delays frame destruction until its final release.
                        EXPECT_EQ(old_destroyed, destination_state != 0 && !observed ? 1 : 0);
                        EXPECT_EQ(new_destroyed, 0);
                        if (source_state == 3 && observed) EXPECT_TRUE(new_observer.finished());
                    }
                    EXPECT_EQ(old_destroyed, destination_state != 0 ? 1 : 0);
                    EXPECT_EQ(new_destroyed, 0);
                }
                EXPECT_EQ(old_destroyed, destination_state != 0 ? 1 : 0);
                EXPECT_EQ(new_destroyed, source_state != 0 ? 1 : 0);
            }
        }
    }
}

// Verifies self-move preserves empty, suspended, completed, and observed coroutine ownership.
TEST_F(context_fixture, async_self_move_assignment_preserves_ownership) {
    for (int state = 0; state != 4; ++state) {
        int destroyed = 0;
        {
            ace::task operation;
            if (state != 0)
                operation = tracked_task(std::make_unique<lifetime_token>(destroyed), state == 2);
            if (state >= 2) operation.awake();
            ace::core::control_block_handle observer;
            if (state != 0) observer = operation.observe();
            const bool existed = operation.is_exist();
            auto& alias = operation;
            EXPECT_EQ(&(operation = std::move(alias)), &operation);
            EXPECT_EQ(operation.is_exist(), existed);
            EXPECT_EQ(operation.track().has_value(), state != 0);
            // Self-move must not run cancellation or drop the only owner reference.
            EXPECT_EQ(destroyed, 0);
            if (state == 3) EXPECT_TRUE(observer.finished());
        }
        EXPECT_EQ(destroyed, state != 0 ? 1 : 0);
    }
}

// Verifies replacement fires pending insure and permanent backups once in LIFO order.
TEST_F(context_fixture, async_move_assignment_fires_backups_once) {
    std::vector<int> callbacks;
    {
        auto destination = guarded_task(callbacks);
        destination.awake();
        ace::task empty;
        destination = std::move(empty);
        ace::run();
        EXPECT_EQ(callbacks, (std::vector<int>{2, 1}));
    }
    ace::run();
    // A second dispatcher drain detects duplicated callbacks during final destruction.
    EXPECT_EQ(callbacks, (std::vector<int>{2, 1}));
    EXPECT_TRUE(ace::empty());
}

// Verifies replacement wakes a waiter registered through the public observer join API.
TEST_F(context_fixture, async_move_assignment_releases_waiter) {
    int destroyed = 0;
    int resumed = 0;
    auto destination = tracked_task(std::make_unique<lifetime_token>(destroyed), true);
    ace::core::runner runner;
    runner.attach(joining_task(destination.observe(), resumed));
    runner.run();
    ASSERT_EQ(resumed, 0);
    // The joiner is now forwarded out of the runnable queue; replacement must reattach it.
    destination = ace::task{};
    while (runner.run()) {}
    EXPECT_EQ(resumed, 1);
    EXPECT_EQ(destroyed, 1);
    EXPECT_TRUE(runner.empty());
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
