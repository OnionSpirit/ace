#include <cstddef>
#include <array>
#include <cstdint>
#include <future>
#include <limits>
#include <list>
#include <new>
#include <thread>
#include <utility>
#include <vector>

#include <sys/uio.h>

#include "environment.h"

#include <ace/core/arena.h>
#include <ace/services/kernelic.h>

#include <nukes/dynamic/regular_freelist.h>

namespace {

struct arena_fixture : ::testing::Test {
    std::size_t saved_max = 0;
    bool saved_breach = true;
    std::size_t saved_runners = 1;

    void SetUp() override {
        saved_max = ace::cfg::g_config._max_allocation_size;
        saved_breach = ace::cfg::g_config._breach_memory_limit;
        saved_runners = ace::cfg::g_config._runners_amount;
    }

    void TearDown() override {
        ace::cfg::g_config._max_allocation_size = saved_max;
        ace::cfg::g_config._breach_memory_limit = saved_breach;
        ace::cfg::g_config._runners_amount = saved_runners;
    }

    template <typename Function>
    static void on_fresh_arena(Function&& function) {
        std::thread(std::forward<Function>(function)).join();
    }
};

// Verifies that is_debug is true without NDEBUG and false when NDEBUG is defined.
TEST_F(arena_fixture, is_debug_matches_build_configuration) {
#ifdef NDEBUG
    EXPECT_FALSE(is_debug);
#else
    EXPECT_TRUE(is_debug);
#endif
}

ace::promise<int> arena_valued_coroutine() {
    co_return 42;
}

struct alignas(256) arena_node_payload {
    std::byte value {};
};

// Verifies pooled accounting, alignment, and retention for one small allocation.
TEST_F(arena_fixture, small_alloc_served_from_pool) {
    ace::cfg::g_config._max_allocation_size = 0;
    on_fresh_arena([] {
        auto& arena = ace::core::arena::get_instance();
        const auto baseline = arena.stats();
        void* pointer = arena.allocate(100);
        const auto allocated = arena.stats();

        EXPECT_NE(nullptr, pointer);
        EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(pointer) % 16);
        // 100 bytes plus the header round up to the 128-byte pool class.
        EXPECT_EQ(128u, allocated.in_use_bytes);
        EXPECT_EQ(0u, allocated.malloc_count);
        EXPECT_GT(allocated.pool_held_bytes, 0u);
        EXPECT_GT(allocated.live_system_chunks, baseline.live_system_chunks);

        arena.deallocate(pointer, 100);
        const auto released = arena.stats();
        EXPECT_EQ(0u, released.in_use_bytes);
        // Pool storage is retained until arena destruction rather than returned now.
        EXPECT_EQ(allocated.pool_held_bytes, released.pool_held_bytes);
        EXPECT_EQ(allocated.live_system_chunks, released.live_system_chunks);
    });
}

// Verifies that an allocation above 4096 bytes uses and releases transient storage.
TEST_F(arena_fixture, big_alloc_goes_to_malloc) {
    ace::cfg::g_config._max_allocation_size = 0;
    on_fresh_arena([] {
        auto& arena = ace::core::arena::get_instance();
        const auto baseline = arena.stats();
        void* pointer = arena.allocate(5000);
        const auto allocated = arena.stats();

        EXPECT_NE(nullptr, pointer);
        EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(pointer) % 16);
        EXPECT_EQ(1u, allocated.malloc_count);
        EXPECT_EQ(baseline.pool_held_bytes, allocated.pool_held_bytes);
        // 5000 bytes plus the header round up to 5024 bytes.
        EXPECT_EQ(5024u, allocated.in_use_bytes - baseline.in_use_bytes);
        EXPECT_EQ(baseline.live_system_chunks + 1, allocated.live_system_chunks);

        arena.deallocate(pointer, 5000);
        const auto released = arena.stats();
        EXPECT_EQ(0u, released.malloc_count);
        EXPECT_EQ(0u, released.in_use_bytes - baseline.in_use_bytes);
        EXPECT_EQ(baseline.live_system_chunks, released.live_system_chunks);
    });
}

// Verifies that a freed pooled chunk is reused without growing upstream storage.
TEST_F(arena_fixture, chunk_reuse_after_free) {
    ace::cfg::g_config._max_allocation_size = 0;
    on_fresh_arena([] {
        auto& arena = ace::core::arena::get_instance();
        void* first = arena.allocate(100);
        arena.deallocate(first, 100);
        const auto before_reuse = arena.stats();
        void* reused = arena.allocate(100);
        const auto after_reuse = arena.stats();

        // Address equality directly observes reuse of the returned pool chunk.
        EXPECT_EQ(first, reused);
        EXPECT_EQ(before_reuse.pool_held_bytes, after_reuse.pool_held_bytes);
        EXPECT_EQ(before_reuse.live_system_chunks, after_reuse.live_system_chunks);
        arena.deallocate(reused, 100);
    });
}

// Verifies that all small size classes retain freed memory until arena destruction.
TEST_F(arena_fixture, pool_never_returns_to_system) {
    ace::cfg::g_config._max_allocation_size = 0;
    on_fresh_arena([] {
        auto& arena = ace::core::arena::get_instance();
        const auto baseline = arena.stats();
        const std::size_t sizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4080};

        std::vector<void*> chunks;
        for (const auto size : sizes)
            chunks.push_back(arena.allocate(size));
        const auto allocated = arena.stats();
        EXPECT_GT(allocated.live_system_chunks, baseline.live_system_chunks);

        for (void* pointer : chunks)
            arena.deallocate(pointer, 0);
        const auto released = arena.stats();
        EXPECT_EQ(0u, released.in_use_bytes);
        EXPECT_EQ(allocated.live_system_chunks, released.live_system_chunks);

        std::vector<void*> reused;
        for (const auto size : sizes)
            reused.push_back(arena.allocate(size));
        const auto reallocated = arena.stats();
        // A second pass must be served entirely from retained pool storage.
        EXPECT_EQ(released.live_system_chunks, reallocated.live_system_chunks);
        EXPECT_EQ(released.pool_held_bytes, reallocated.pool_held_bytes);
        for (void* pointer : reused)
            arena.deallocate(pointer, 0);
    });
}

// Verifies that arena::get_instance returns one distinct singleton per thread.
TEST_F(arena_fixture, arena_is_thread_local) {
    auto& main_arena = ace::core::arena::get_instance();
    void* worker_arena = nullptr;
    std::thread worker([&worker_arena] {
        worker_arena = &ace::core::arena::get_instance();
    });
    worker.join();

    EXPECT_NE(&main_arena, worker_arena);
    EXPECT_EQ(&main_arena, &ace::core::arena::get_instance());
}

// Verifies that a foreign pooled free returns to and is reused by its owner arena.
TEST_F(arena_fixture, cross_thread_free_returns_to_owner) {
    ace::cfg::g_config._max_allocation_size = 0;
    std::promise<void*> chunk_ready;
    std::promise<void> freed;
    auto chunk_future = chunk_ready.get_future();
    auto freed_future = freed.get_future();

    std::thread owner([&] {
        auto& arena = ace::core::arena::get_instance();
        void* first = arena.allocate(256);
        chunk_ready.set_value(first);
        freed_future.wait();
        void* reused = arena.allocate(256);
        // max=0 drains the foreign-release channel before this allocation.
        EXPECT_EQ(first, reused);
        arena.deallocate(reused, 256);
    });
    std::thread foreign([&] {
        ace::core::arena::get_instance().deallocate(chunk_future.get(), 256);
        freed.set_value();
    });
    owner.join();
    foreign.join();
}

// Verifies the max/occupied release-channel drain cadence at utilization N=2.
TEST_F(arena_fixture, channel_drain_cadence) {
    ace::cfg::g_config._max_allocation_size = 65536;
    ace::cfg::g_config._runners_amount = 1;
    std::promise<void*> chunk_ready;
    std::promise<void> freed;
    auto chunk_future = chunk_ready.get_future();
    auto freed_future = freed.get_future();

    std::thread owner([&] {
        auto& arena = ace::core::arena::get_instance();
        constexpr auto size = 4080u;
        std::vector<void*> chunks;
        for (int i = 0; i < 7; ++i)
            chunks.push_back(arena.allocate(size));
        const auto initial_drains = arena.stats().drain_count;

        void* foreign_chunk = arena.allocate(size);
        EXPECT_EQ(initial_drains, arena.stats().drain_count);
        chunk_ready.set_value(foreign_chunk);
        freed_future.wait();

        void* reused = arena.allocate(size);
        EXPECT_EQ(initial_drains + 1, arena.stats().drain_count);
        EXPECT_EQ(foreign_chunk, reused);
        void* next = arena.allocate(size);
        // The next odd cadence operation must not drain again.
        EXPECT_EQ(initial_drains + 1, arena.stats().drain_count);

        arena.deallocate(reused, size);
        arena.deallocate(next, size);
        for (void* pointer : chunks)
            arena.deallocate(pointer, size);
    });
    std::thread foreign([&] {
        ace::core::arena::get_instance().deallocate(chunk_future.get(), 4080);
        freed.set_value();
    });
    owner.join();
    foreign.join();
}

// Verifies that max=0 drains the foreign-release channel before every allocation.
TEST_F(arena_fixture, limit_zero_drains_every_alloc) {
    ace::cfg::g_config._max_allocation_size = 0;
    std::promise<void*> chunk_ready;
    std::promise<void> freed;
    auto chunk_future = chunk_ready.get_future();
    auto freed_future = freed.get_future();

    std::thread owner([&] {
        auto& arena = ace::core::arena::get_instance();
        constexpr auto size = 256u;
        void* first = arena.allocate(size);
        chunk_ready.set_value(first);
        freed_future.wait();
        void* reused = arena.allocate(size);
        EXPECT_EQ(2u, arena.stats().drain_count);
        EXPECT_EQ(first, reused);
        arena.deallocate(reused, size);
    });
    std::thread foreign([&] {
        ace::core::arena::get_instance().deallocate(chunk_future.get(), 256);
        freed.set_value();
    });
    owner.join();
    foreign.join();
}

// Verifies the occupied=0 drain branch and subsequent utilization cadence.
TEST_F(arena_fixture, occupied_zero_drains_every_alloc) {
    ace::cfg::g_config._max_allocation_size = 65536;
    std::promise<void*> chunk_ready;
    std::promise<void> freed;
    auto chunk_future = chunk_ready.get_future();
    auto freed_future = freed.get_future();

    std::thread owner([&] {
        auto& arena = ace::core::arena::get_instance();
        constexpr auto size = 256u;
        void* foreign_chunk = arena.allocate(size);
        EXPECT_EQ(1u, arena.stats().drain_count);
        chunk_ready.set_value(foreign_chunk);
        freed_future.wait();

        void* local = arena.allocate(size);
        EXPECT_EQ(1u, arena.stats().drain_count);
        // The second operation does not drain, so it cannot reuse foreign_chunk.
        EXPECT_NE(foreign_chunk, local);
        arena.deallocate(local, size);
    });
    std::thread foreign([&] {
        ace::core::arena::get_instance().deallocate(chunk_future.get(), 256);
        freed.set_value();
    });
    owner.join();
    foreign.join();
}

// Verifies transient fallback, bad_alloc policy, and requested-size limit checks.
TEST_F(arena_fixture, limit_breach_fallback_malloc) {
    const auto baseline_chunks = ace::core::arena::live_system_chunks.load();
    ace::cfg::g_config._max_allocation_size = 4096;
    ace::cfg::g_config._runners_amount = 1;
    ace::cfg::g_config._breach_memory_limit = true;
    on_fresh_arena([] {
        auto& arena = ace::core::arena::get_instance();
        constexpr auto size = 4080u;
        void* pooled = arena.allocate(size);
        const auto before_breach = arena.stats();
        void* transient = arena.allocate(size);
        const auto breached = arena.stats();

        EXPECT_EQ(1u, breached.malloc_count);
        EXPECT_EQ(before_breach.pool_held_bytes, breached.pool_held_bytes);
        EXPECT_EQ(before_breach.live_system_chunks + 1, breached.live_system_chunks);
        arena.deallocate(transient, size);
        EXPECT_EQ(0u, arena.stats().malloc_count);
        EXPECT_EQ(breached.live_system_chunks - 1, arena.stats().live_system_chunks);
        arena.deallocate(pooled, size);
        EXPECT_EQ(0u, arena.stats().in_use_bytes);
    });
    EXPECT_EQ(baseline_chunks, ace::core::arena::live_system_chunks.load());

    ace::cfg::g_config._breach_memory_limit = false;
    on_fresh_arena([] {
        auto& arena = ace::core::arena::get_instance();
        constexpr auto size = 4080u;
        void* first = arena.allocate(size);
        EXPECT_THROW(static_cast<void>(arena.allocate(size)), std::bad_alloc);
        arena.deallocate(first, size);
    });

    ace::cfg::g_config._max_allocation_size = 4096;
    on_fresh_arena([] {
        auto& arena = ace::core::arena::get_instance();
        void* first = arena.allocate(3000);
        // occupied is below the limit, but occupied + requested exceeds it.
        EXPECT_THROW(static_cast<void>(arena.allocate(2000)), std::bad_alloc);
        arena.deallocate(first, 3000);
    });
}

// Verifies that thread exit releases pooled, queued, and transient arena storage.
TEST_F(arena_fixture, destructor_returns_everything) {
    const auto baseline_chunks = ace::core::arena::live_system_chunks.load();
    std::promise<void*> chunk_ready;
    std::promise<void> freed;
    auto chunk_future = chunk_ready.get_future();
    auto freed_future = freed.get_future();

    std::thread owner([&] {
        auto& arena = ace::core::arena::get_instance();
        constexpr auto size = 4080u;
        std::vector<void*> chunks;
        for (int i = 0; i < 8; ++i)
            chunks.push_back(arena.allocate(size));
        for (void* pointer : chunks)
            arena.deallocate(pointer, size);
        void* transient = arena.allocate(5000);
        arena.deallocate(transient, 5000);
        void* queued = arena.allocate(size);
        chunk_ready.set_value(queued);
        freed_future.wait();
    });
    std::thread foreign([&] {
        ace::core::arena::get_instance().deallocate(chunk_future.get(), 4080);
        freed.set_value();
    });
    owner.join();
    foreign.join();

    EXPECT_EQ(baseline_chunks, ace::core::arena::live_system_chunks.load());
}

// Verifies that outstanding pooled and transient chunks keep owner storage alive after thread exit.
TEST_F(arena_fixture, owner_storage_outlives_departed_thread) {
    ace::cfg::g_config._max_allocation_size = 0;
    // Initialize the releasing thread's arena before taking the process-wide
    // baseline: libstdc++ may obtain a pool bookkeeping block eagerly.
    auto& foreign = ace::core::arena::get_instance();
    const auto baseline_chunks = ace::core::arena::live_system_chunks.load();
    std::promise<std::array<void*, 2>> chunks_ready;
    auto chunks_future = chunks_ready.get_future();
    std::thread owner([&] {
        auto& arena = ace::core::arena::get_instance();
        chunks_ready.set_value({arena.allocate(256), arena.allocate(5000)});
    });
    owner.join();

    const auto chunks = chunks_future.get();
    EXPECT_GT(ace::core::arena::live_system_chunks.load(), baseline_chunks);
    foreign.deallocate(chunks[0], 256);
    foreign.deallocate(chunks[1], 5000);
    EXPECT_EQ(baseline_chunks, ace::core::arena::live_system_chunks.load());
}

// Verifies that a coroutine frame is allocated and released through the shared arena.
TEST_F(arena_fixture, promise_traits_uses_arena) {
    auto& arena = ace::core::arena::get_instance();
    const auto baseline = arena.stats();
    {
        auto coroutine = arena_valued_coroutine();
        const auto allocated = arena.stats();
        EXPECT_GT(allocated.in_use_bytes, baseline.in_use_bytes);
        EXPECT_GE(allocated.pool_held_bytes, baseline.pool_held_bytes);
        EXPECT_EQ(0u, allocated.malloc_count);
        // The frame includes an aligned control block before the promise.
        EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(coroutine._coroutine.address()) % 16);
    }
    EXPECT_EQ(baseline.in_use_bytes, arena.stats().in_use_bytes);
}

// Verifies deferred owner accounting after a foreign transient free.
TEST_F(arena_fixture, foreign_transient_free_updates_owner_accounting) {
    ace::cfg::g_config._max_allocation_size = 0;
    std::promise<void*> chunk_ready;
    std::promise<void> freed;
    auto chunk_future = chunk_ready.get_future();
    auto freed_future = freed.get_future();

    std::thread owner([&] {
        auto& arena = ace::core::arena::get_instance();
        void* transient = arena.allocate(5000);
        EXPECT_EQ(5024u, arena.stats().in_use_bytes);
        EXPECT_EQ(1u, arena.stats().malloc_count);
        chunk_ready.set_value(transient);
        freed_future.wait();

        const auto pending = arena.stats();
        EXPECT_EQ(0u, pending.malloc_count);
        EXPECT_EQ(5024u, pending.in_use_bytes);
        void* local = arena.allocate(100);
        // The allocation drains released_bytes before adding its own 128 bytes.
        EXPECT_EQ(128u, arena.stats().in_use_bytes);
        EXPECT_EQ(0u, arena.stats().malloc_count);
        arena.deallocate(local, 100);
    });
    std::thread foreign([&] {
        ace::core::arena::get_instance().deallocate(chunk_future.get(), 5000);
        freed.set_value();
    });
    owner.join();
    foreign.join();
}

// Verifies typed pooled and transient allocations through arena::allocate_as.
TEST_F(arena_fixture, typed_allocation_uses_arena) {
    ace::cfg::g_config._max_allocation_size = 0;
    on_fresh_arena([] {
        auto& arena = ace::core::arena::get_instance();
        const auto baseline = arena.stats();
        auto* small = arena.allocate_as<std::uint64_t>(8);
        auto* large = arena.allocate_as<std::byte>(5000);
        ASSERT_NE(nullptr, small);
        ASSERT_NE(nullptr, large);
        EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(small) % alignof(std::uint64_t));
        EXPECT_GT(arena.stats().in_use_bytes, baseline.in_use_bytes);
        EXPECT_EQ(1u, arena.stats().malloc_count);

        arena.deallocate_as(small, 8);
        arena.deallocate_as(large, 5000);
        EXPECT_EQ(baseline.in_use_bytes, arena.stats().in_use_bytes);
        EXPECT_EQ(0u, arena.stats().malloc_count);
    });
}

// Verifies that typed allocation detects count multiplication overflow first.
TEST_F(arena_fixture, typed_allocation_overflow) {
    on_fresh_arena([] {
        auto& arena = ace::core::arena::get_instance();
        const auto count = std::numeric_limits<std::size_t>::max() / sizeof(std::uint64_t) + 1;
        EXPECT_THROW(
            static_cast<void>(arena.allocate_as<std::uint64_t>(count)),
            std::bad_array_new_length);
        // Detection occurs before arena accounting changes.
        EXPECT_EQ(0u, arena.stats().in_use_bytes);
    });
}

// Verifies ACE configures Nukes to use its durable node arena with over-aligned storage.
TEST_F(arena_fixture, nukes_node_allocator_uses_durable_arena_and_preserves_overalignment) {
    ASSERT_TRUE(ace::core::configure_nukes_node_allocator());
    const auto baseline = ace::core::nukes_node_arena::outstanding_bytes();
    {
        nukes::dynamic::reg_freelist<arena_node_payload> freelist;
        arena_node_payload* payload = nullptr;
        ASSERT_TRUE(freelist.capture(payload));
        ASSERT_NE(nullptr, payload);
        EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(payload) % alignof(arena_node_payload));
        ASSERT_TRUE(freelist.release(payload));
        EXPECT_GT(ace::core::nukes_node_arena::outstanding_bytes(), baseline);
    }
    EXPECT_EQ(baseline, ace::core::nukes_node_arena::outstanding_bytes());
}

// Verifies arena_allocator storage and contents in a node-based standard container.
TEST_F(arena_fixture, arena_allocator_list_storage) {
    ace::cfg::g_config._max_allocation_size = 0;
    on_fresh_arena([] {
        auto& arena = ace::core::arena::get_instance();
        const auto baseline = arena.stats();
        {
            std::list<int, ace::core::arena_allocator<int>> values;
            for (int i = 0; i < 32; ++i)
                values.push_back(i);
            EXPECT_EQ(32u, values.size());
            EXPECT_EQ(0, values.front());
            EXPECT_EQ(31, values.back());
            EXPECT_GT(arena.stats().in_use_bytes, baseline.in_use_bytes);
        }
        EXPECT_EQ(baseline.in_use_bytes, arena.stats().in_use_bytes);
    });
}

// Verifies that kernel iovec helpers allocate from the same singleton arena.
TEST_F(arena_fixture, iovec_uses_shared_arena) {
    ace::cfg::g_config._max_allocation_size = 0;
    on_fresh_arena([] {
        auto& arena = ace::core::arena::get_instance();
        const auto baseline = arena.stats();
        auto* data_iovec = ace::services::kernel_controller::iovec_allocate(64);
        auto* array_iovec = ace::services::kernel_controller::iovec_pool_allocate(300);
        EXPECT_GT(arena.stats().in_use_bytes, baseline.in_use_bytes);
        // The 300-iovec array is larger than 4096 bytes and is transient.
        EXPECT_EQ(1u, arena.stats().malloc_count);

        ace::services::kernel_controller::iovec_deallocate(data_iovec);
        ace::services::kernel_controller::iovec_pool_deallocate(array_iovec, 300);
        EXPECT_EQ(baseline.in_use_bytes, arena.stats().in_use_bytes);
    });
}

// Verifies cross-thread return and owner-side reuse of iovec backing storage.
TEST_F(arena_fixture, cross_thread_iovec_release) {
    ace::cfg::g_config._max_allocation_size = 0;
    std::promise<::iovec*> chunk_ready;
    std::promise<void> freed;
    auto chunk_future = chunk_ready.get_future();
    auto freed_future = freed.get_future();

    std::thread owner([&] {
        auto* first = ace::services::kernel_controller::iovec_allocate(64);
        chunk_ready.set_value(first);
        freed_future.wait();
        auto* reused = ace::services::kernel_controller::iovec_allocate(64);
        // Address equality proves the owner drained and reused the foreign return.
        EXPECT_EQ(first, reused);
        ace::services::kernel_controller::iovec_deallocate(reused);
    });
    std::thread foreign([&] {
        ace::services::kernel_controller::iovec_deallocate(chunk_future.get());
        freed.set_value();
    });
    owner.join();
    foreign.join();
}

} // namespace
