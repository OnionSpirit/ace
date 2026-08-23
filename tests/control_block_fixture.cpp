#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <new>

#include <gtest/gtest.h>

#include <ace/core/async.h>
#include <ace/core/control.h>
#include <ace/core/traits/promise.h>

struct control_block_fixture : ::testing::Test {
    struct mini_promise
        : ace::core::traits::promise_traits<mini_promise, ace::core::lazy_rule, void> {
        DECLARE_PROMISE_TRAITS(mini_promise, ace::core::lazy_rule, void)
        IMPORT_PROMISE_TRAITS_ENV

        mini_promise() = default;

        static auto initial_suspend() noexcept { return std::suspend_always {}; }
        static auto final_suspend() noexcept { return std::suspend_always {}; }
        void return_void() {}
        void unhandled_exception() {}

        auto get_return_object() noexcept {
            return ace::core::async<void, ace::core::lazy_rule> {};
        }
    };

    struct allocated_promise {
        ace::core::control_block* block;
        mini_promise* promise;

        allocated_promise() {
            constexpr std::size_t control_size = sizeof(ace::core::control_block);
            constexpr std::size_t promise_size = sizeof(mini_promise);
            auto* raw = static_cast<std::uint8_t*>(::operator new(control_size + promise_size));
            block = ::new (raw) ace::core::control_block();
            promise = ::new (raw + control_size) mini_promise();
            promise->_block = block;
        }

        ~allocated_promise() {
            promise->~mini_promise();
            block->~control_block();
            ::operator delete(block);
        }

        auto get_handle() {
            return std::coroutine_handle<mini_promise>::from_promise(*promise);
        }
    };
};

// Verifies the default reference count, frame marker, and lifecycle state.
TEST_F(control_block_fixture, control_block_init) {
    ace::core::control_block block;
    EXPECT_EQ(1u, block._refcount);
    EXPECT_EQ(0u, block._frame_size);
    EXPECT_EQ(ace::core::e_inited, block._status);
}

// Verifies that removing the final tracked reference marks the block untracked.
TEST_F(control_block_fixture, unwatch_last) {
    ace::core::control_block block;
    const bool untracked = ace::core::control_block::untrack(&block);

    EXPECT_TRUE(untracked);
    EXPECT_TRUE(ace::core::control_block::is_untracked(&block));
}

// Verifies balanced tracking increments and decrements the reference count.
TEST_F(control_block_fixture, watch_unwatch) {
    ace::core::control_block block;
    EXPECT_FALSE(ace::core::control_block::track(&block));
    EXPECT_EQ(2u, block._refcount);

    EXPECT_FALSE(ace::core::control_block::untrack(&block));
    EXPECT_EQ(1u, block._refcount);
}

// Verifies that only a zero reference count is considered untracked.
TEST_F(control_block_fixture, is_untracked) {
    ace::core::control_block block;
    EXPECT_FALSE(ace::core::control_block::is_untracked(&block));

    block._refcount = 0;
    EXPECT_TRUE(ace::core::control_block::is_untracked(&block));
}

// Verifies recovery of the control block immediately preceding a promise address.
TEST_F(control_block_fixture, get_block_from_address) {
    constexpr auto control_size = sizeof(ace::core::control_block);
    auto* raw = new std::uint8_t[control_size + 64];
    ::new (raw) ace::core::control_block();
    void* promise_address = raw + control_size;

    auto* block = ace::core::control_block::get_block_from_address(promise_address);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(raw), reinterpret_cast<std::uintptr_t>(block));
    delete[] raw;
}

// Verifies that a default control handle is idle and not complete.
TEST_F(control_block_fixture, control_block_handle_default) {
    ace::core::control_block_handle handle;
    EXPECT_TRUE(handle.is_idle());
    EXPECT_FALSE(handle.done());
    EXPECT_FALSE(handle.finished());
}

// Verifies that cancel on an idle handle is a safe no-op.
TEST_F(control_block_fixture, handle_cancel_no_router) {
    ace::core::control_block_handle handle;
    EXPECT_NO_THROW(handle.cancel());
}

// Verifies that copying a handle tracks an additional reference until destruction.
TEST_F(control_block_fixture, handle_copy) {
    allocated_promise allocation;
    EXPECT_EQ(1u, allocation.block->_refcount);
    {
        auto coroutine = allocation.get_handle();
        ace::core::control_block_handle first(coroutine);
        EXPECT_EQ(2u, allocation.block->_refcount);
        {
            ace::core::control_block_handle second(first);
            EXPECT_EQ(3u, allocation.block->_refcount);
        }
        EXPECT_EQ(2u, allocation.block->_refcount);
    }
    EXPECT_EQ(1u, allocation.block->_refcount);
}

// Verifies that a failed lifecycle state is reported as done.
TEST_F(control_block_fixture, handle_done) {
    allocated_promise allocation;
    auto coroutine = allocation.get_handle();
    ace::core::control_block_handle handle(coroutine);
    EXPECT_FALSE(handle.done());

    allocation.block->_status = ace::core::e_failed;
    EXPECT_TRUE(handle.done());
}

// Verifies that only successful completion is reported as finished.
TEST_F(control_block_fixture, handle_finished) {
    allocated_promise allocation;
    auto coroutine = allocation.get_handle();
    ace::core::control_block_handle handle(coroutine);
    EXPECT_FALSE(handle.finished());

    allocation.block->_status = ace::core::e_finished;
    EXPECT_TRUE(handle.finished());
}

// Verifies the idle predicate for a handle with no referenced block.
TEST_F(control_block_fixture, handle_is_idle) {
    ace::core::control_block_handle handle;
    EXPECT_TRUE(handle.is_idle());
}

// Verifies that forwarding rejects a null waiter before router dispatch.
TEST_F(control_block_fixture, handle_forward_null) {
    allocated_promise allocation;
    auto coroutine = allocation.get_handle();
    ace::core::control_block_handle handle(coroutine);

    EXPECT_FALSE(handle.forward(nullptr));
}

// Verifies that forwarding rejects a waiter when no control router is installed.
TEST_F(control_block_fixture, handle_forward_done) {
    allocated_promise allocation;
    // Preserve the original cleared-frame setup while exercising the no-router guard.
    allocation.block->_frame_size = 0;
    auto coroutine = allocation.get_handle();
    ace::core::control_block_handle handle(coroutine);

    EXPECT_FALSE(handle.forward(allocation.promise));
}

// Verifies that destroying a handle releases its tracked reference.
TEST_F(control_block_fixture, handle_destroy) {
    allocated_promise allocation;
    EXPECT_EQ(1u, allocation.block->_refcount);
    {
        auto coroutine = allocation.get_handle();
        ace::core::control_block_handle handle(coroutine);
        EXPECT_EQ(2u, allocation.block->_refcount);
    }
    EXPECT_EQ(1u, allocation.block->_refcount);
}
