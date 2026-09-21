#include <thread>

#include <gtest/gtest.h>
#include <ace/ace.h>
#include <ace/core/traits/service.h>
#include "allocation_failure.h"

namespace {
thread_local int slab_events = 0;
void count_slab_events(bool) { ++slab_events; }

struct toolkit_alpha : ace::core::traits::service_traits<toolkit_alpha, ace::core::service_spawn_mode::e_thread_local> {
    bool ping() { return false; }
};
struct toolkit_beta : ace::core::traits::service_traits<toolkit_beta, ace::core::service_spawn_mode::e_thread_local> {
    bool ping() { return false; }
};
}

// Verifies one debug slab hook covers different payload types, but only on its own thread.
TEST(testing_toolkit_fixture, slab_hook_is_shared_across_types_and_thread_local) {
    slab_events = 0;
    int other_thread_events = -1;
    {
        const slab_failure_scope hook {count_slab_events};
        ace::core::tools::slab_mempool<int> ints;
        ace::core::tools::slab_mempool<double> doubles;
        ints.free(ints.alloc());
        doubles.free(doubles.alloc());
        EXPECT_EQ(4, slab_events); // Allocation and registration for each of two slabs.
        std::thread other([&] {
            ace::core::tools::slab_mempool<int> pool;
            pool.free(pool.alloc());
            other_thread_events = slab_events;
        });
        other.join();
    }
    EXPECT_EQ(0, other_thread_events);
    ace::core::tools::slab_mempool<char> after_restore;
    after_restore.free(after_restore.alloc());
    EXPECT_EQ(4, slab_events);
}

// Verifies service hook state is distinct per CRTP specialization and permits retry after restoration.
TEST(testing_toolkit_fixture, service_hook_is_isolated_per_specialization) {
    ace::cfg::g_config._runners_amount = 1;
    ASSERT_TRUE(ace::reload());
    {
        const service_failure_scope<toolkit_alpha> hook;
        EXPECT_NO_THROW(toolkit_beta::touch(nullptr));
        EXPECT_THROW(toolkit_alpha::touch(nullptr), std::bad_alloc);
        ace::run();
    }
    EXPECT_NO_THROW(toolkit_alpha::touch(nullptr));
    ace::run();
    EXPECT_TRUE(ace::empty());
}
