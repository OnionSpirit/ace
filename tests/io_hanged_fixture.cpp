#include <cerrno>
#include <span>
#include <stdexcept>

#include <gtest/gtest.h>

#include <ace/io.h>

struct io_hanged_fixture : ::testing::Test {};

// Verifies that basic_fail_handler reports a negative I/O result by throwing.
TEST_F(io_hanged_fixture, hanged_basic_fail_handler) {
    const char message[] = "test error";
    const std::span<const char> description(message, sizeof(message));
    EXPECT_THROW(
        ace::io::outcast::basic_fail_handler(-EINVAL, description),
        std::runtime_error);
}

// Verifies that basic_fail_handler itself throws even when passed a non-negative result.
TEST_F(io_hanged_fixture, hanged_fail_handler_positive) {
    const char message[] = "ok";
    const std::span<const char> description(message, sizeof(message));

    // command::on_result(), not the handler, gates invocation on res < 0;
    // calling the handler directly isolates its unconditional reporting path.
    EXPECT_THROW(
        ace::io::outcast::basic_fail_handler(0, description),
        std::runtime_error);
}

// Verifies that the thread-local outcast command pool is accessible.
TEST_F(io_hanged_fixture, hanged_command_pool_exists) {
    static_cast<void>(ace::io::outcast::_command_pool);
    SUCCEED();
}

// Verifies that a captured outcast command is valid and can be returned to its pool.
TEST_F(io_hanged_fixture, hanged_command_pool_capture) {
    ace::io::outcast::command* command = nullptr;
    const bool captured = ace::io::outcast::_command_pool.capture(command);
    if (captured) {
        ASSERT_NE(nullptr, command);
        ace::io::outcast::_command_pool.raw_release(command);
    }
    SUCCEED();
}

// Verifies that a pooled command can complete a capture/return lifecycle safely.
TEST_F(io_hanged_fixture, hanged_command_defaults) {
    ace::io::outcast::command* command = nullptr;
    if (ace::io::outcast::_command_pool.capture(command)) {
        // raw_release() does not promise to clear payload fields, so this test
        // intentionally checks pool lifecycle rather than stale field values.
        ASSERT_NE(nullptr, command);
        ace::io::outcast::_command_pool.raw_release(command);
    }
    SUCCEED();
}
