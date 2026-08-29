#include <stdexcept>

#include <gtest/gtest.h>

#include <ace/core/async.h>
#include <ace/core/traits/routing.h>

struct router_slot_fixture : ::testing::Test {
    struct test_router : ace::core::traits::runner_router_handle<ace::omni_node> {
        static int alive_count;

        test_router() { ++alive_count; }
        test_router(const test_router&) { ++alive_count; }
        test_router(test_router&&) noexcept { ++alive_count; }
        ~test_router() override { --alive_count; }

        static void reset_counter() { alive_count = 0; }

        bool redirect(ace::omni_node) override { return true; }
        void cancel() override {}
    };

    using slot_t = ace::core::traits::router_slot<
        ace::core::traits::runner_router_handle<ace::omni_node>
    >;

    void TearDown() override {
        test_router::reset_counter();
    }
};

int router_slot_fixture::test_router::alive_count = 0;

// Verifies that a default router slot is empty and exposes no router.
TEST_F(router_slot_fixture, router_slot_empty) {
    slot_t slot;
    EXPECT_FALSE(slot);
    EXPECT_EQ(nullptr, slot.get());
}

// Verifies move assignment of a concrete router into slot storage.
TEST_F(router_slot_fixture, router_slot_assign_move) {
    slot_t slot;
    test_router::reset_counter();
    slot = test_router {};

    EXPECT_TRUE(slot);
    EXPECT_NE(nullptr, slot.get());
}

// Verifies copy assignment of a concrete router into slot storage.
TEST_F(router_slot_fixture, router_slot_assign_copy) {
    slot_t slot;
    test_router::reset_counter();
    test_router router;
    slot = router;

    EXPECT_TRUE(slot);
}

// Verifies that operator<< transfers ownership and clears the source slot.
TEST_F(router_slot_fixture, router_slot_steal) {
    slot_t source;
    source = test_router {};
    EXPECT_TRUE(source);
    slot_t destination;

    destination << source;
    EXPECT_FALSE(source);
    EXPECT_TRUE(destination);
    destination.release();
}

// Verifies that release destroys the stored router and empties the slot.
TEST_F(router_slot_fixture, router_slot_release) {
    test_router::reset_counter();
    slot_t slot;
    slot = test_router {};
    EXPECT_EQ(1, test_router::alive_count);

    slot.release();
    EXPECT_FALSE(slot);
    EXPECT_EQ(0, test_router::alive_count);
}

// Verifies that reset clears the discriminant without destroying storage.
TEST_F(router_slot_fixture, router_slot_reset) {
    test_router::reset_counter();
    slot_t slot;
    slot = test_router {};
    EXPECT_EQ(1, test_router::alive_count);

    slot.reset();
    EXPECT_FALSE(slot);
    // reset is the ownership-transfer path, so destruction must not occur here.
    EXPECT_EQ(1, test_router::alive_count);
    slot.release();
}

// Verifies that releasing an already empty slot is a safe no-op.
TEST_F(router_slot_fixture, router_slot_release_twice) {
    slot_t slot;
    slot = test_router {};
    slot.release();
    EXPECT_FALSE(slot);

    slot.release();
    EXPECT_FALSE(slot);
}

// Verifies that an unimplemented base redirect reports programmer error.
TEST_F(router_slot_fixture, redirect_not_overridden) {
    ace::core::traits::runner_router_handle<ace::omni_node> router;
    EXPECT_THROW(router.redirect(ace::omni_node {}), std::logic_error);
}

// Verifies that the base cancel implementation remains a no-op.
TEST_F(router_slot_fixture, runner_router_handle_default_cancel) {
    ace::core::traits::runner_router_handle<ace::omni_node> router;
    EXPECT_NO_THROW(router.cancel());
}
