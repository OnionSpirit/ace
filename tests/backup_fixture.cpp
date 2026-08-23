#include <chrono>
#include <stdexcept>
#include <vector>

#include "environment.h"

#include <ace/futures/backup.h>
#include <ace/futures/roaming.h>
#include <ace/futures/spawn.h>
#include <ace/futures/timeout.h>

namespace {

struct backup_fixture : base_fixture {
    void TearDown() override {
        ace::cfg::g_config._emergency_default = true;
    }

    ace::bus<int> channel;
};

ace::task triple_backup_sleeper(std::vector<int>& order) {
    co_await ace::backup([&order] { order.push_back(1); });
    co_await ace::backup([&order] { order.push_back(2); });
    co_await ace::backup([&order] { order.push_back(3); });
    co_await ace::timeout(std::chrono::seconds(10));
}

ace::task cancel_triple_backups(std::vector<int>& order, ace::bus<int>& result) {
    auto handle = co_await ace::spawn(triple_backup_sleeper(order));
    co_await ace::timeout(std::chrono::milliseconds(10));
    handle.cancel();
    result << ((co_await handle.join()) ? 0 : 1);
}

ace::task many_backup_sleeper(std::vector<int>& order, int count) {
    for (int i = 0; i < count; ++i)
        co_await ace::backup([&order, i] { order.push_back(i); });
    co_await ace::timeout(std::chrono::seconds(10));
}

ace::task cancel_many_backups(
    std::vector<int>& order,
    int count,
    ace::bus<int>& result)
{
    auto handle = co_await ace::spawn(many_backup_sleeper(order, count));
    co_await ace::timeout(std::chrono::milliseconds(10));
    handle.cancel();
    result << ((co_await handle.join()) ? 0 : 1);
}

ace::task complete_with_backups(std::vector<int>& order) {
    co_await ace::backup([&order] { order.push_back(1); });
    co_await ace::backup([&order] { order.push_back(2); });
    co_return;
}

ace::promise<int> incomplete_eager_backup(std::vector<int>& order) {
    co_await ace::backup([&order] { order.push_back(7); });
    base_fixture::once_suspend blocker;
    co_await blocker;
    co_return 42;
}

ace::task scheduled_fire_child(ace::bus<int>& result) {
    co_await ace::backup([&result] { result << 1; });
    co_await ace::timeout(std::chrono::seconds(10));
}

ace::task scheduled_fire_driver(ace::bus<int>& result) {
    auto handle = co_await ace::spawn(scheduled_fire_child(result));
    co_await ace::timeout(std::chrono::milliseconds(10));
    handle.cancel();
    result << 100;
    result << ((co_await handle.join()) ? 0 : 1);
}

ace::task task_payload_two(std::vector<int>& order) {
    co_await ace::timeout(std::chrono::milliseconds(5));
    order.push_back(2);
}

ace::task task_payload_backup_child(std::vector<int>& order) {
    co_await ace::backup([&order] { order.push_back(1); });
    co_await ace::backup(task_payload_two(order));
    co_await ace::backup([&order] { order.push_back(3); });
    co_await ace::timeout(std::chrono::seconds(10));
}

ace::task cancel_task_payload(std::vector<int>& order, ace::bus<int>& result) {
    auto handle = co_await ace::spawn(task_payload_backup_child(order));
    co_await ace::timeout(std::chrono::milliseconds(10));
    handle.cancel();
    result << ((co_await handle.join()) ? 0 : 1);
}

ace::task protected_timeout(std::vector<int>& order) {
    co_await ace::insure([&order] { order.push_back(1); });
    co_await ace::timeout(std::chrono::seconds(10));
}

ace::task cancel_protected_timeout(std::vector<int>& order, ace::bus<int>& result) {
    auto handle = co_await ace::spawn(protected_timeout(order));
    co_await ace::timeout(std::chrono::milliseconds(10));
    handle.cancel();
    result << ((co_await handle.join()) ? 0 : 1);
}

ace::task passed_protected_timeout(std::vector<int>& order) {
    co_await ace::insure([&order] { order.push_back(1); });
    co_await ace::timeout(std::chrono::milliseconds(5));
    co_await ace::timeout(std::chrono::seconds(10));
}

ace::task cancel_after_protected_timeout(std::vector<int>& order, ace::bus<int>& result) {
    auto handle = co_await ace::spawn(passed_protected_timeout(order));
    co_await ace::timeout(std::chrono::milliseconds(20));
    handle.cancel();
    result << ((co_await handle.join()) ? 0 : 1);
}

ace::task ready_protected_operation(std::vector<int>& order) {
    co_await ace::insure([&order] { order.push_back(1); });
    co_await ace::roaming(false);
    co_await ace::timeout(std::chrono::seconds(10));
}

ace::task cancel_after_ready_operation(std::vector<int>& order, ace::bus<int>& result) {
    auto handle = co_await ace::spawn(ready_protected_operation(order));
    co_await ace::timeout(std::chrono::milliseconds(10));
    handle.cancel();
    result << ((co_await handle.join()) ? 0 : 1);
}

ace::task insure_replaced_by_backup_child(std::vector<int>& order) {
    co_await ace::insure([&order] { order.push_back(1); });
    co_await ace::backup([&order] { order.push_back(2); });
    co_await ace::timeout(std::chrono::seconds(10));
}

ace::task cancel_insure_replaced_by_backup(std::vector<int>& order, ace::bus<int>& result) {
    auto handle = co_await ace::spawn(insure_replaced_by_backup_child(order));
    co_await ace::timeout(std::chrono::milliseconds(10));
    handle.cancel();
    result << ((co_await handle.join()) ? 0 : 1);
}

ace::task insure_replaced_by_insure_child(std::vector<int>& order) {
    co_await ace::insure([&order] { order.push_back(1); });
    co_await ace::insure([&order] { order.push_back(2); });
    co_await ace::timeout(std::chrono::seconds(10));
}

ace::task cancel_insure_replaced_by_insure(std::vector<int>& order, ace::bus<int>& result) {
    auto handle = co_await ace::spawn(insure_replaced_by_insure_child(order));
    co_await ace::timeout(std::chrono::milliseconds(10));
    handle.cancel();
    result << ((co_await handle.join()) ? 0 : 1);
}

ace::automaton<int> yield_after_insure(std::vector<int>& order) {
    co_await ace::insure([&order] { order.push_back(1); });
    co_yield 10;
    co_return 42;
}

ace::automaton<int> yield_insure_then_timeout(std::vector<int>& order) {
    co_await ace::insure([&order] { order.push_back(1); });
    co_yield 10;
    co_await ace::timeout(std::chrono::milliseconds(100));
    co_return 42;
}

ace::task cancel_insured_yield(
    std::vector<int>& order,
    ace::bus<int>& result)
{
    auto automaton = yield_after_insure(order);
    auto handle = automaton.observe();
    result << co_await automaton;
    handle.cancel();
    (void)co_await automaton;
    result << 99;
}

ace::task pass_insured_yield(
    std::vector<int>& order,
    ace::bus<int>& result)
{
    auto automaton = yield_insure_then_timeout(order);
    auto handle = automaton.observe();
    result << co_await automaton;
    (void)co_await automaton;
    handle.cancel();
    result << 99;
}

ace::task insure_loop_child(std::vector<int>& order) {
    for (int i = 0; i < 9; ++i) {
        co_await ace::insure([&order, i] { order.push_back(i); });
        co_await ace::timeout(std::chrono::milliseconds(5));
    }
    co_await ace::insure([&order] { order.push_back(99); });
    co_await ace::timeout(std::chrono::seconds(10));
}

ace::task cancel_insure_loop(std::vector<int>& order, ace::bus<int>& result) {
    auto handle = co_await ace::spawn(insure_loop_child(order));
    co_await ace::timeout(std::chrono::milliseconds(100));
    handle.cancel();
    result << ((co_await handle.join()) ? 0 : 1);
}

ace::task throwing_backup(std::vector<int>& order) {
    co_await ace::backup([&order] { order.push_back(1); });
    throw std::runtime_error("backup_fixture: intentional exception");
}

ace::task throwing_backup_with_emergency(std::vector<int>& order, bool enabled) {
    co_await ace::emergency(enabled);
    co_await ace::backup([&order] { order.push_back(1); });
    throw std::runtime_error("backup_fixture: intentional exception");
}

ace::automaton<int> yield_after_backup(std::vector<int>& order) {
    co_await ace::backup([&order] { order.push_back(1); });
    co_yield 10;
    co_return 42;
}

ace::task cancel_backup_automaton(std::vector<int>& order, ace::bus<int>& result) {
    auto automaton = yield_after_backup(order);
    auto handle = automaton.observe();
    result << co_await automaton;
    handle.cancel();
    (void)co_await automaton;
    result << 99;
}

ace::task spawned_backup_child(std::vector<int>& order) {
    co_await ace::backup([&order] { order.push_back(1); });
    co_await ace::timeout(std::chrono::seconds(10));
}

ace::task cancel_spawned_backup(std::vector<int>& order, ace::bus<int>& result) {
    auto handle = co_await ace::spawn(spawned_backup_child(order));
    co_await ace::timeout(std::chrono::milliseconds(10));
    handle.cancel();
    result << ((co_await handle.join()) ? 0 : 1);
}

// Verifies that cancel fires three backup callbacks in LIFO order.
TEST_F(backup_fixture, backup_cancel_fires_lifo) {
    std::vector<int> order;
    ace::schedule(cancel_triple_backups(order, channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
    ASSERT_EQ(3u, order.size());
    // Reverse registration order is the backup stack's public contract.
    EXPECT_EQ(3, order[0]);
    EXPECT_EQ(2, order[1]);
    EXPECT_EQ(1, order[2]);
}

// Verifies LIFO behavior across many arena-backed backup records.
TEST_F(backup_fixture, backup_stack_many_records_fires_lifo) {
    constexpr int records = 64;
    std::vector<int> order;
    ace::schedule(cancel_many_backups(order, records, channel));
    ace::run();

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
    ASSERT_EQ(static_cast<std::size_t>(records), order.size());
    for (int i = 0; i < records; ++i)
        EXPECT_EQ(records - i - 1, order[static_cast<std::size_t>(i)]);
}

// Verifies that normal co_return discards backups without firing them.
TEST_F(backup_fixture, backup_normal_completion_no_fire) {
    std::vector<int> order;
    ace::schedule(complete_with_backups(order));
    ace::run();
    EXPECT_TRUE(ace::empty());
    EXPECT_TRUE(order.empty());
}

// Verifies that destroying an incomplete eager promise schedules its backup fire task.
TEST_F(backup_fixture, backup_destroy_incomplete_fires) {
    std::vector<int> order;
    {
        auto promise = incomplete_eager_backup(order);
        EXPECT_TRUE(promise);
        EXPECT_TRUE(order.empty());
    }
    // No runner existed at destruction, so the fallback must remain deferred.
    EXPECT_TRUE(order.empty());
    ace::run();
    ASSERT_EQ(1u, order.size());
    EXPECT_EQ(7, order[0]);
}

// Verifies that backup callbacks are scheduled rather than invoked inline by cancel.
TEST_F(backup_fixture, backup_fire_scheduled_not_inline) {
    ace::schedule(scheduled_fire_driver(channel));
    ace::run();

    const auto values = fetch(channel);
    ASSERT_EQ(3u, values.size());
    // Driver marker 100 precedes callback marker 1 only for scheduled fire.
    EXPECT_EQ(100, values[0]);
    EXPECT_EQ(1, values[1]);
    EXPECT_EQ(1, values[2]);
}

// Verifies that task-valued backups are awaited while preserving mixed LIFO order.
TEST_F(backup_fixture, backup_task_payload_awaited) {
    std::vector<int> order;
    ace::schedule(cancel_task_payload(order, channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
    ASSERT_EQ(3u, order.size());
    // Marker 2 appears only after the task payload's own timeout completes.
    EXPECT_EQ(3, order[0]);
    EXPECT_EQ(2, order[1]);
    EXPECT_EQ(1, order[2]);
}

// Verifies that insure fires when cancellation occurs on its protected await.
TEST_F(backup_fixture, insure_fires_when_cancelled_during_protected_await) {
    std::vector<int> order;
    ace::schedule(cancel_protected_timeout(order, channel));
    ace::run();

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
    ASSERT_EQ(1u, order.size());
    EXPECT_EQ(1, order[0]);
}

// Verifies that insure is removed after its protected await completes.
TEST_F(backup_fixture, insure_dropped_after_passing_protected_await) {
    std::vector<int> order;
    ace::schedule(cancel_after_protected_timeout(order, channel));
    ace::run();

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
    // Cancellation occurs on the later timeout, after insure has been passed.
    EXPECT_TRUE(order.empty());
}

// Verifies insure removal when the protected operation completes synchronously.
TEST_F(backup_fixture, insure_dropped_when_next_op_ready) {
    std::vector<int> order;
    ace::schedule(cancel_after_ready_operation(order, channel));
    ace::run();

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
    // roaming(false) is ready; cancel on the following timeout must not fire insure.
    EXPECT_TRUE(order.empty());
}

// Verifies that registering backup replaces an outstanding insure record.
TEST_F(backup_fixture, insure_replaced_by_backup) {
    std::vector<int> order;
    ace::schedule(cancel_insure_replaced_by_backup(order, channel));
    ace::run();

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
    ASSERT_EQ(1u, order.size());
    EXPECT_EQ(2, order[0]);
}

// Verifies that a newer insure replaces the previous insure record.
TEST_F(backup_fixture, insure_replaced_by_insure) {
    std::vector<int> order;
    ace::schedule(cancel_insure_replaced_by_insure(order, channel));
    ace::run();

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
    ASSERT_EQ(1u, order.size());
    EXPECT_EQ(2, order[0]);
}

// Verifies that insure fires when an automaton is canceled on its protected yield.
TEST_F(backup_fixture, insure_automaton_yield_fires) {
    std::vector<int> order;
    ace::schedule(cancel_insured_yield(order, channel));
    ace::run();

    const auto values = fetch(channel);
    ASSERT_EQ(2u, values.size());
    EXPECT_EQ(10, values[0]);
    EXPECT_EQ(99, values[1]);
    ASSERT_EQ(1u, order.size());
    EXPECT_EQ(1, order[0]);
}

// Verifies that advancing past a protected yield removes the automaton insure.
TEST_F(backup_fixture, insure_automaton_yield_dropped) {
    std::vector<int> order;
    ace::schedule(pass_insured_yield(order, channel));
    ace::run();

    const auto values = fetch(channel);
    ASSERT_EQ(2u, values.size());
    EXPECT_EQ(10, values[0]);
    EXPECT_EQ(99, values[1]);
    // The second await advances beyond co_yield before cancellation.
    EXPECT_TRUE(order.empty());
}

// Verifies that repeated insure operations retain only the currently protected record.
TEST_F(backup_fixture, insure_loop_bounded) {
    std::vector<int> order;
    ace::schedule(cancel_insure_loop(order, channel));
    ace::run();

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
    ASSERT_EQ(1u, order.size());
    // If passed records accumulated, markers 0 through 8 would also appear.
    EXPECT_EQ(99, order[0]);
}

// Verifies that the default emergency policy fires backups after an exception.
TEST_F(backup_fixture, emergency_default_exception_fires) {
    std::vector<int> order;
    ace::schedule(throwing_backup(order));
    ace::run();
    EXPECT_TRUE(ace::empty());
    ASSERT_EQ(1u, order.size());
    EXPECT_EQ(1, order[0]);
}

// Verifies that explicit emergency(true) fires backups after an exception.
TEST_F(backup_fixture, emergency_true_exception_fires) {
    std::vector<int> order;
    ace::schedule(throwing_backup_with_emergency(order, true));
    ace::run();
    ASSERT_EQ(1u, order.size());
    EXPECT_EQ(1, order[0]);
}

// Verifies that emergency(false) suppresses backup fire on exception.
TEST_F(backup_fixture, emergency_false_exception_no_fire) {
    std::vector<int> order;
    ace::schedule(throwing_backup_with_emergency(order, false));
    ace::run();
    EXPECT_TRUE(order.empty());
}

// Verifies that new coroutines inherit the configured default emergency policy.
TEST_F(backup_fixture, emergency_config_default) {
    ace::cfg::g_config._emergency_default = false;
    std::vector<int> order;
    ace::schedule(throwing_backup(order));
    ace::run();
    // TearDown restores the global default for subsequent tests.
    EXPECT_TRUE(order.empty());
}

// Verifies that canceling an automaton on co_yield fires its persistent backup.
TEST_F(backup_fixture, backup_in_automaton_cancel) {
    std::vector<int> order;
    ace::schedule(cancel_backup_automaton(order, channel));
    ace::run();

    const auto values = fetch(channel);
    ASSERT_EQ(2u, values.size());
    EXPECT_EQ(10, values[0]);
    EXPECT_EQ(99, values[1]);
    ASSERT_EQ(1u, order.size());
    EXPECT_EQ(1, order[0]);
}

// Verifies that async_handle::cancel fires backups registered by its spawned task.
TEST_F(backup_fixture, backup_cancel_via_spawn_handle) {
    std::vector<int> order;
    ace::schedule(cancel_spawned_backup(order, channel));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(channel);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
    ASSERT_EQ(1u, order.size());
    EXPECT_EQ(1, order[0]);
}

} // namespace
