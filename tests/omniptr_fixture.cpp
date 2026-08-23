#include <concepts>
#include <utility>

#include <gtest/gtest.h>

#include <ace/core/tools/lifetime.h>
#include <ace/core/tools/omniptr.h>

namespace tool = ace::core::tools;

struct omniptr_fixture : ::testing::Test {};

// Verifies that a default omniptr represents a null pointer.
TEST_F(omniptr_fixture, default_construction) {
    tool::omniptr<int, double> pointer;
    EXPECT_FALSE(pointer);
}

// Verifies typed construction and explicit typed access.
TEST_F(omniptr_fixture, typed_construction) {
    int value = 42;
    tool::omniptr<int, double> pointer(&value);

    EXPECT_TRUE(pointer);
    EXPECT_EQ(&value, pointer.as<int>());
}

// Verifies construction from the erased void-pointer representation.
TEST_F(omniptr_fixture, void_star_construction) {
    int value = 10;
    void* raw = &value;
    tool::omniptr<int, double> pointer(raw);

    EXPECT_EQ(&value, pointer.as<int>());
}

// Verifies that copying preserves the stored address.
TEST_F(omniptr_fixture, copy_construction) {
    int value = 7;
    tool::omniptr<int, double> source(&value);
    tool::omniptr<int, double> copy(source);

    EXPECT_EQ(source.as<int>(), copy.as<int>());
}

// Verifies that moving transfers the address and clears the source.
TEST_F(omniptr_fixture, move_construction) {
    int value = 5;
    tool::omniptr<int, double> source(&value);
    tool::omniptr<int, double> destination(std::move(source));

    EXPECT_EQ(&value, destination.as<int>());
    EXPECT_FALSE(source);
}

// Verifies implicit conversion to an allowed mutable pointer type.
TEST_F(omniptr_fixture, implicit_conversion) {
    int value = 3;
    tool::omniptr<int, double> pointer(&value);
    int* typed = pointer;

    EXPECT_EQ(&value, typed);
}

// Verifies implicit conversion from a const omniptr to a const pointer.
TEST_F(omniptr_fixture, const_conversion) {
    int value = 1;
    const tool::omniptr<int, double> pointer(&value);
    const int* typed = pointer;

    EXPECT_EQ(&value, typed);
}

// Verifies mutable and const conversions to the erased pointer type.
TEST_F(omniptr_fixture, void_star_conversion) {
    int value = 99;
    tool::omniptr<int, double> pointer(&value);
    void* raw = pointer;
    const void* const_raw = static_cast<const tool::omniptr<int, double>&>(pointer);

    EXPECT_EQ(&value, raw);
    EXPECT_EQ(&value, const_raw);
}

// Verifies that operator-> accesses the first allowed pointer type.
TEST_F(omniptr_fixture, arrow_operator) {
    struct record {
        int value = 10;
    };

    record object;
    tool::omniptr<record, int> pointer(&object);
    EXPECT_EQ(10, pointer->value);
}

// Verifies equality for identical and distinct stored addresses.
TEST_F(omniptr_fixture, equality) {
    int first = 1;
    tool::omniptr<int, double> lhs(&first);
    tool::omniptr<int, double> rhs(&first);
    EXPECT_TRUE(lhs == rhs);

    int second = 2;
    tool::omniptr<int, double> other(&second);
    EXPECT_FALSE(lhs == other);
}

// Verifies that lifetime::mark returns the construction marker unchanged.
TEST_F(omniptr_fixture, lifetime_mark) {
    tool::lifetime lifetime("test_marker");
    EXPECT_EQ("test_marker", lifetime.mark());
}

// Verifies that enabling and disabling lifetime tracking preserves the marker.
TEST_F(omniptr_fixture, lifetime_track) {
    tool::lifetime::track();
    {
        tool::lifetime lifetime("tracked_object");
        EXPECT_EQ("tracked_object", lifetime.mark());
    }
    tool::lifetime::untrack();
    SUCCEED();
}
