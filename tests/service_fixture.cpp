#include <cstdint>
#include <thread>

#include "environment.h"

#include <ace/core/traits/service.h>

namespace {

struct local_test_service
    : ace::core::traits::service_traits<
          local_test_service,
          ace::core::service_spawn_mode::e_thread_local> {

    static bool ping() { return false; }
};

struct shared_test_service
    : ace::core::traits::service_traits<
          shared_test_service,
          ace::core::service_spawn_mode::e_thread_shared> {

    static bool ping() { return false; }
};

struct service_fixture : base_fixture {};

// Verifies inspect observes the touch-owned instance without scheduling it.
TEST_F(service_fixture, inspect_and_touch_share_instance_without_eager_respawn) {
    auto* const inspected = &local_test_service::inspect();

    // A pure inspection must not enqueue the polling coroutine; touch is the
    // explicit lifecycle operation that performs the first respawn.
    EXPECT_TRUE(ace::empty());
    auto* const touched = &local_test_service::touch(nullptr);
    EXPECT_EQ(inspected, touched);
    EXPECT_FALSE(ace::empty());

    ace::run();
    EXPECT_TRUE(ace::empty());
}

// Verifies thread-local services expose a distinct inspected instance per OS thread.
TEST_F(service_fixture, inspect_uses_distinct_thread_local_instances) {
    const auto current = reinterpret_cast<std::uintptr_t>(&local_test_service::inspect());
    std::uintptr_t worker = 0;

    std::thread thread([&worker] {
        worker = reinterpret_cast<std::uintptr_t>(&local_test_service::inspect());
    });
    thread.join();

    // Distinct addresses prove inspect follows the declared thread-local spawn
    // mode instead of returning a hidden process-wide singleton.
    EXPECT_NE(0U, worker);
    EXPECT_NE(current, worker);
}

// Verifies thread-shared services expose one inspected instance to every thread.
TEST_F(service_fixture, inspect_uses_one_thread_shared_instance) {
    const auto current = reinterpret_cast<std::uintptr_t>(&shared_test_service::inspect());
    std::uintptr_t worker = 0;

    std::thread thread([&worker] {
        worker = reinterpret_cast<std::uintptr_t>(&shared_test_service::inspect());
    });
    thread.join();

    // Shared mode must preserve one identity even when no service coroutine is
    // respawned by either inspecting thread.
    EXPECT_EQ(current, worker);
    EXPECT_TRUE(ace::empty());
}

} // namespace
