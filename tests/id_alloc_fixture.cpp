#include <gtest/gtest.h>

#include <ace/core/tools/id_alloc.h>

namespace tool = ace::core::tools;

struct id_alloc_fixture : ::testing::Test {};

// Verifies that released IDs are recycled on subsequent allocations.
TEST_F(id_alloc_fixture, id_alloc_free_cycle) {
    tool::id_allocator allocator;
    const auto first = allocator.id_alloc();
    allocator.id_free(first);
    const auto second = allocator.id_alloc();
    EXPECT_EQ(first, second);

    // A second recycle proves the queue remains usable after one pop/push cycle.
    allocator.id_free(second);
    const auto third = allocator.id_alloc();
    EXPECT_EQ(first, third);
}

// Verifies that unreleased sequential allocations produce unique IDs.
TEST_F(id_alloc_fixture, id_alloc_unique) {
    tool::id_allocator allocator;
    const auto first = allocator.id_alloc();
    const auto second = allocator.id_alloc();
    const auto third = allocator.id_alloc();

    EXPECT_NE(first, second);
    EXPECT_NE(second, third);
    EXPECT_NE(first, third);
    allocator.id_free(first);
    allocator.id_free(second);
    allocator.id_free(third);
}

// Verifies allocation and release through the global async ID allocator.
TEST_F(id_alloc_fixture, async_id_allocator) {
    auto& allocator = tool::async_id_allocator::get_instance();
    const auto id = allocator.id_alloc();

    EXPECT_GE(id, 0u);
    allocator.id_free(id);
}
