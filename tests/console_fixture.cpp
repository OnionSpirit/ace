#include <gtest/gtest.h>

#include <ace/ace.h>
#include <ace/console.h>

struct console_fixture : ::testing::Test {};

// Verifies that the short println(string_view) alias accepts ordinary text.
TEST_F(console_fixture, println_string_view) {
    // ace.h precedes console.h so this exercises the same implementation via
    // the intentionally exported ace::println alias.
    EXPECT_NO_THROW(ace::println("test println"));
}

// Verifies that the zero-argument println overload emits an empty line safely.
TEST_F(console_fixture, println_empty) {
    EXPECT_NO_THROW(ace::println());
}

// Verifies that the short print(string_view) alias accepts text without a newline.
TEST_F(console_fixture, print_string_view) {
    EXPECT_NO_THROW(ace::print("test print"));
}

// Verifies that the formatted print overload accepts and substitutes an integer.
TEST_F(console_fixture, print_format) {
    EXPECT_NO_THROW(ace::print("value = {}", 42));
}
