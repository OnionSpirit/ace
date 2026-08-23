#include <coroutine>
#include <type_traits>
#include <variant>

#include <gtest/gtest.h>

#include <ace/core/async.h>
#include <ace/core/dispatcher.h>
#include <ace/core/tools/id_alloc.h>
#include <ace/futures/timeout.h>

namespace tool = ace::core::tools;

struct promise_traits_fixture : ::testing::Test {
    ace::promise<int> simple_valued_coroutine() {
        co_return 42;
    }
};

// Verifies that the eager promise rule starts without an initial suspension.
TEST_F(promise_traits_fixture, permanent_tag_action) {
    static_assert(
        std::same_as<
            decltype(ace::core::eager_rule<std::monostate>::initial_result()),
            std::suspend_never
        >
    );
    SUCCEED();
}

// Verifies that the lazy async rule suspends before running its body.
TEST_F(promise_traits_fixture, differed_tag_action) {
    static_assert(
        std::same_as<
            decltype(ace::core::lazy_rule<std::monostate>::initial_result()),
            std::suspend_always
        >
    );
    SUCCEED();
}

// Verifies that automata are lazy and suspend before their first execution.
TEST_F(promise_traits_fixture, automaton_tag_action) {
    // This assertion protects the creation contract, independently of later co_yield suspensions.
    static_assert(
        std::same_as<
            decltype(ace::core::automaton_rule<std::monostate>::initial_result()),
            std::suspend_always
        >
    );
    SUCCEED();
}

// Verifies that a default void task is an empty coroutine object.
TEST_F(promise_traits_fixture, return_traits_void) {
    ace::task task;
    EXPECT_FALSE(task.is_exist());
}

// Verifies that a typed co_return completes through task_wrap and the dispatcher.
TEST_F(promise_traits_fixture, return_traits_typed) {
    // Running the wrapper exercises the compiler-generated return_value path.
    ace::schedule(ace::task_wrap(simple_valued_coroutine()));
    ace::run();
    EXPECT_TRUE(ace::empty());
    SUCCEED();
}

// Verifies that timeout uses router-based future dispatch, not busy polling.
TEST_F(promise_traits_fixture, await_transform_future) {
    static_assert(
        not ace::core::meta::is_busy_future_accurate<
            ace::futures::timeout,
            ace::task::promise_type
        >,
        "timeout is not a busy future"
    );
    SUCCEED();
}

// Verifies that coroutine allocation places a discoverable control block prefix.
TEST_F(promise_traits_fixture, operator_new_layout) {
    auto coroutine = simple_valued_coroutine();
    if (coroutine._coroutine) {
        auto* promise_address = coroutine._coroutine.address();
        auto* block = ace::core::control_block::get_block_from_address(promise_address);
        ASSERT_NE(nullptr, block);
        // The eager coroutine has completed, so its frame is marked disowned.
        EXPECT_EQ(0u, block->_frame_size);
    }
}

// Verifies that the shared trace allocator returns increasing fresh IDs.
TEST_F(promise_traits_fixture, setup_trace) {
    auto& allocator = tool::async_id_allocator::get_instance();
    const auto first = allocator.id_alloc();
    const auto second = allocator.id_alloc();

    EXPECT_LT(first, second);
    allocator.id_free(first);
    allocator.id_free(second);
}
