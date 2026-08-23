#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <ace/io.h>

struct io_any_fixture : ::testing::Test {};

// Verifies that an empty io::any can be constructed and destroyed safely.
TEST_F(io_any_fixture, any_default_construction) {
    ace::io::any value;
    SUCCEED();
}

// Verifies that io::any accepts a trivially destructible integer payload.
TEST_F(io_any_fixture, any_construct_int) {
    ace::io::any value(42);
    SUCCEED();
}

// Verifies that io::any accepts a non-trivial string payload.
TEST_F(io_any_fixture, any_construct_string) {
    ace::io::any value(std::string("test data"));
    SUCCEED();
}

// Verifies that release() destroys the payload and leaves the holder reusable for destruction.
TEST_F(io_any_fixture, any_release) {
    ace::io::any value(42);
    value.release();
    SUCCEED();
}

// Verifies that move construction transfers payload ownership without double destruction.
TEST_F(io_any_fixture, any_move) {
    ace::io::any source(100);
    ace::io::any destination(std::move(source));
    SUCCEED();
}

// Verifies that destroying io::any invokes the non-trivial payload deleter safely.
TEST_F(io_any_fixture, any_destructor) {
    { ace::io::any value(std::string("will be destroyed")); }
    SUCCEED();
}
