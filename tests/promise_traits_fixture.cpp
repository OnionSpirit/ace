#include <algorithm>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

    static ace::task named_argument_coroutine(
        const std::uint64_t value,
        const std::uint64_t& referenced,
        std::uint64_t& observed)
    {
        observed = value + referenced;
        co_return;
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

// Verifies that coroutine allocation stores the exact immutable frame size in the prefix block.
TEST_F(promise_traits_fixture, operator_new_layout) {
    using promise_type = ace::task::promise_type;
    constexpr std::size_t requested_frame_size = 257;
    constexpr std::size_t expected_allocation_size =
        requested_frame_size + ace::core::control_block_size;

    void* frame = promise_type::operator new(requested_frame_size);
    auto* block = ace::core::control_block::get_block_from_address(frame);

    ASSERT_NE(nullptr, block);
    EXPECT_EQ(expected_allocation_size, block->_frame_size);

    // Direct allocation does not construct a promise, so release the initial block reference
    // before invoking the matching sized deallocator.
    EXPECT_TRUE(ace::core::control_block::untrack(block));
    promise_type::operator delete(frame, requested_frame_size);
}

// Verifies that prefix metadata initialization does not overwrite the coroutine frame payload.
TEST_F(promise_traits_fixture, operator_new_preserves_frame_canary) {
    using promise_type = ace::task::promise_type;
    constexpr std::size_t requested_frame_size = 257;
    constexpr std::size_t allocation_size =
        requested_frame_size + ace::core::control_block_size;
    constexpr unsigned char canary = 0xa5;

    auto& arena = ace::core::arena::get_instance();
    void* seeded_allocation = arena.allocate(allocation_size);
    std::memset(seeded_allocation, canary, allocation_size);
    arena.deallocate(seeded_allocation, allocation_size);

    void* frame = promise_type::operator new(requested_frame_size);
    auto* block = ace::core::control_block::get_block_from_address(frame);
    const bool reused_seeded_allocation = block == seeded_allocation;
    EXPECT_TRUE(reused_seeded_allocation);
    if (reused_seeded_allocation) {
        const auto* frame_bytes = static_cast<const unsigned char*>(frame);
        EXPECT_TRUE(std::all_of(
            frame_bytes,
            frame_bytes + requested_frame_size,
            [](const unsigned char byte) { return byte == canary; }
        ));
    }

    EXPECT_TRUE(ace::core::control_block::untrack(block));
    promise_type::operator delete(frame, requested_frame_size);
}

// Verifies that observe() preserves explicit arguments in a named lazy coroutine.
TEST_F(promise_traits_fixture, observe_preserves_named_coroutine_arguments) {
    std::uint64_t value = 0x1122334455667788;
    const std::uint64_t referenced = 0x0102030405060708;
    std::uint64_t observed = 0;
    auto coroutine = named_argument_coroutine(value, referenced, observed);
    auto* block = ace::core::control_block::get_block_from_address(coroutine._coroutine.address());
    const auto frame_size = block->_frame_size;
    auto observer = coroutine.observe();

    EXPECT_GT(frame_size, ace::core::control_block_size);
    EXPECT_FALSE(observer.is_idle());
    coroutine.awake();
    EXPECT_EQ(value + referenced, observed);
    EXPECT_TRUE(observer.finished());
    EXPECT_EQ(frame_size, block->_frame_size);
}

// Verifies that observe() preserves value and reference captures of a live coroutine lambda.
TEST_F(promise_traits_fixture, observe_preserves_lambda_coroutine_captures) {
    std::uint64_t value = 0x1122334455667788;
    std::uint64_t referenced = 0x0102030405060708;
    std::uint64_t observed = 0;
    // The named closure intentionally outlives both the returned task and its observer.
    auto coroutine_factory = [value, &referenced, &observed]() -> ace::task {
        observed = value + referenced;
        co_return;
    };
    auto coroutine = coroutine_factory();
    auto* block = ace::core::control_block::get_block_from_address(coroutine._coroutine.address());
    const auto frame_size = block->_frame_size;
    auto observer = coroutine.observe();

    EXPECT_GT(frame_size, ace::core::control_block_size);
    EXPECT_FALSE(observer.is_idle());
    coroutine.awake();
    EXPECT_EQ(value + referenced, observed);
    EXPECT_TRUE(observer.finished());
    EXPECT_EQ(frame_size, block->_frame_size);
}

// Verifies lambda captures remain valid through suspension, observation, and cancellation.
TEST_F(promise_traits_fixture, observed_lambda_coroutine_cancels_safely) {
    std::uint64_t value = 0x1122334455667788;
    std::uint64_t referenced = 0x0102030405060708;
    std::uint64_t observed = 0;
    // Keeping the closure alive isolates ACE frame handling from the standard closure-lifetime rule.
    auto coroutine_factory = [value, &referenced, &observed]() -> ace::task {
        observed = value + referenced;
        co_await std::suspend_always {};
        observed = 0;
        co_return;
    };
    auto coroutine = coroutine_factory();
    auto* block = ace::core::control_block::get_block_from_address(coroutine._coroutine.address());
    const auto frame_size = block->_frame_size;
    auto observer = coroutine.observe();

    EXPECT_GT(frame_size, ace::core::control_block_size);
    coroutine.awake();
    EXPECT_EQ(value + referenced, observed);
    EXPECT_FALSE(observer.done());

    observer.cancel();
    EXPECT_TRUE(observer.is_idle());
    EXPECT_EQ(value + referenced, observed);
    EXPECT_EQ(frame_size, block->_frame_size);
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
