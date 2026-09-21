#include <array>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <type_traits>

#include <gtest/gtest.h>

#include <nukes/details/node_types.h>
#include <nukes/dynamic/mpmc_freelist.h>
#include <nukes/dynamic/regular_freelist.h>
#include <nukes/dynamic/spmc_freelist.h>

namespace {

struct nukes_alignment_fixture : ::testing::Test {};

struct lifetime_payload {
    static inline const lifetime_payload* watched = nullptr;
    static inline int watched_destructions = 0;

    ~lifetime_payload() {
        if (this == watched)
            ++watched_destructions;
    }
};

template <typename freelist_t, typename data_t>
concept has_legacy_sync = requires(freelist_t& freelist, data_t*& data) {
    freelist.sync(data);
};

static_assert(not has_legacy_sync<nukes::dynamic::mpmc_freelist<int>, int>);
static_assert(not has_legacy_sync<nukes::dynamic::spmc_freelist<int>, int>);
static_assert(not has_legacy_sync<nukes::dynamic::reg_freelist<int>, int>);

template <std::size_t alignment>
struct alignas(alignment) aligned_payload {
    std::byte value {};
};

template <typename node_t, typename payload_t>
void expect_node_alignment(payload_t* payload) {
    static_assert(std::is_standard_layout_v<node_t>,
                  "offsetof requires a standard-layout freelist node");
    ASSERT_NE(nullptr, payload);
    auto* const node = reinterpret_cast<node_t*>(
        reinterpret_cast<std::uintptr_t>(payload) - offsetof(node_t, _data));

    // The payload's address alone is insufficient: its offset can hide a
    // misaligned node allocation.  Recover the owning node to assert the
    // allocator meets the complete node contract.
    EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(node) % alignof(node_t));
    EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(payload) % alignof(payload_t));
}

template <template <typename, std::size_t> typename freelist_t,
          template <typename> typename node_template, std::size_t alignment>
void capture_sync_capture_alignment() {
    using payload_t = aligned_payload<alignment>;
    using node_t = node_template<payload_t>;
    freelist_t<payload_t, 0> freelist;

    payload_t* first = nullptr;
    ASSERT_TRUE(freelist.capture(first));
    expect_node_alignment<node_t>(first);

    // Release the storage and acquire it again so both allocation growth and
    // recycled-node ownership paths retain the same alignment guarantee.
    ASSERT_TRUE(freelist.release(first));
    EXPECT_EQ(nullptr, first);

    payload_t* second = nullptr;
    ASSERT_TRUE(freelist.capture(second));
    expect_node_alignment<node_t>(second);
    ASSERT_TRUE(freelist.release(second));
    EXPECT_EQ(nullptr, second);
}

// Verifies release destroys the payload and makes the caller's pointer unusable.
TEST_F(nukes_alignment_fixture, release_destroys_payload_and_clears_pointer) {
    lifetime_payload::watched = nullptr;
    lifetime_payload::watched_destructions = 0;
    nukes::dynamic::reg_freelist<lifetime_payload> freelist;

    lifetime_payload* payload = nullptr;
    ASSERT_TRUE(freelist.capture(payload));
    ASSERT_NE(nullptr, payload);
    lifetime_payload::watched = payload;

    ASSERT_TRUE(freelist.release(payload));
    EXPECT_EQ(nullptr, payload);
    EXPECT_EQ(1, lifetime_payload::watched_destructions);
}

// Verifies raw_release preserves a live payload while returning its node to the pool.
TEST_F(nukes_alignment_fixture, raw_release_preserves_payload_lifetime) {
    lifetime_payload::watched = nullptr;
    lifetime_payload::watched_destructions = 0;
    nukes::dynamic::reg_freelist<lifetime_payload> freelist;

    lifetime_payload* first = nullptr;
    ASSERT_TRUE(freelist.capture(first));
    ASSERT_NE(nullptr, first);
    lifetime_payload::watched = first;
    ASSERT_TRUE(freelist.raw_release(first));
    EXPECT_EQ(0, lifetime_payload::watched_destructions);

    lifetime_payload* second = nullptr;
    ASSERT_TRUE(freelist.capture(second));
    EXPECT_EQ(first, second);
    ASSERT_TRUE(freelist.release(second));
    EXPECT_EQ(1, lifetime_payload::watched_destructions);
}

template <template <typename, std::size_t> typename freelist_t,
          template <typename> typename node_template>
void exercise_supported_alignments() {
    capture_sync_capture_alignment<freelist_t, node_template, 1>();
    capture_sync_capture_alignment<freelist_t, node_template, 8>();
    capture_sync_capture_alignment<freelist_t, node_template, 16>();
    capture_sync_capture_alignment<freelist_t, node_template, 32>();
    capture_sync_capture_alignment<freelist_t, node_template, 64>();
    capture_sync_capture_alignment<freelist_t, node_template, 128>();
    capture_sync_capture_alignment<freelist_t, node_template, 256>();
}

// Verifies MPMC freelist storage meets every supported node-alignment contract.
TEST_F(nukes_alignment_fixture, mpmc_freelist_preserves_overaligned_nodes) {
    exercise_supported_alignments<nukes::dynamic::mpmc_freelist,
                                 nukes::detail::nodes::dyn_node>();
}

// Verifies SPMC freelist storage meets every supported node-alignment contract.
TEST_F(nukes_alignment_fixture, spmc_freelist_preserves_overaligned_nodes) {
    exercise_supported_alignments<nukes::dynamic::spmc_freelist,
                                 nukes::detail::nodes::dyn_node>();
}

// Verifies regular freelist storage meets every supported node-alignment contract.
TEST_F(nukes_alignment_fixture, regular_freelist_preserves_overaligned_nodes) {
    exercise_supported_alignments<nukes::dynamic::reg_freelist,
                                 nukes::detail::nodes::dyn_reg_node>();
}

} // namespace
