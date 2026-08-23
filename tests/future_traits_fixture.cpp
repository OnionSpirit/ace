#include <concepts>
#include <string>
#include <tuple>
#include <variant>

#include <gtest/gtest.h>

#include <ace/ace.h>
#include <ace/core/async.h>
#include <ace/core/traits/future.h>

#include <ace/futures/cutex.h>
#include <ace/futures/timeout.h>

// cutex_fixture.cpp owns the strong definitions emitted by cutex.h.
asm(".weak _ZN3ace7futures13cutex_control8try_lockEv\n"
    ".weak _ZN3ace7futures13cutex_control6notifyEv\n"
    ".weak _ZN3ace7futures13cutex_control14pending_notifyEv\n"
    ".weak _ZN3ace7futures5cutex7captureEb\n"
    ".weak _ZN3ace7futures5cutex7releaseEv");

struct future_traits_fixture : ::testing::Test {
    struct once_suspend : ace::core::traits::busy_future_traits<once_suspend> {
        IMPORT_BUSY_FUTURE_ENV(once_suspend)

        bool _trigger {false};

        bool await_ready() override {
            if (not _trigger) {
                _trigger = true;
                return false;
            }
            return true;
        }

        void await_suspend(auto) {}
        void await_resume() {}
    };
};

// Verifies that ACE task and promise types satisfy the awaitable concept.
TEST_F(future_traits_fixture, is_awaitable_concept) {
    static_assert(
        ace::core::meta::is_awaitable<ace::task, ace::task::promise_type>,
        "ace::task must satisfy is_awaitable concept"
    );
    static_assert(
        ace::core::meta::is_awaitable<ace::promise<int>, ace::task::promise_type>,
        "ace::promise<int> must satisfy is_awaitable concept"
    );
    SUCCEED();
}

// Verifies that timeout and cutex capture operations are regular futures.
TEST_F(future_traits_fixture, is_future_concept) {
    static_assert(
        ace::core::meta::is_future<ace::futures::timeout>,
        "timeout must satisfy is_future concept"
    );
    static_assert(
        ace::core::meta::is_future<ace::futures::capture_future>,
        "capture_future must satisfy is_future concept"
    );
    SUCCEED();
}

// Verifies that the polling helper satisfies the accurate busy-future concept.
TEST_F(future_traits_fixture, is_busy_future_concept) {
    // The promise type is part of this check because busy-future dispatch is promise-specific.
    static_assert(
        ace::core::meta::is_busy_future_accurate<once_suspend, ace::task::promise_type>,
        "once_suspend must satisfy is_busy_future concept"
    );
    SUCCEED();
}

// Verifies replacement of void while leaving unrelated types unchanged.
TEST_F(future_traits_fixture, replace_type) {
    using ace::core::meta::replace_type;
    static_assert(std::same_as<replace_type<void, void, std::monostate>, std::monostate>);
    static_assert(std::same_as<replace_type<int, void, std::monostate>, int>);
    static_assert(std::same_as<replace_type<double, int, std::monostate>, double>);
    SUCCEED();
}

// Verifies that unique_tuple_t removes repeated types in first-seen order.
TEST_F(future_traits_fixture, unique_tuple) {
    using input = std::tuple<int, int, double, int>;
    using expected = std::tuple<int, double>;
    static_assert(std::same_as<ace::core::meta::unique_tuple_t<input>, expected>);
    SUCCEED();
}

// Verifies conversion from a tuple of types to the matching variant.
TEST_F(future_traits_fixture, tuple_to_variant) {
    using input = std::tuple<int, std::string>;
    using expected = std::variant<int, std::string>;
    static_assert(std::same_as<ace::core::meta::tuple_to_variant_t<input>, expected>);
    SUCCEED();
}

// Verifies type lookup at the first and last tested parameter-pack positions.
TEST_F(future_traits_fixture, at_pack) {
    using ace::core::meta::at_pack;
    static_assert(std::same_as<at_pack<0, int, double, char>, int>);
    static_assert(std::same_as<at_pack<2, int, double, char>, char>);
    SUCCEED();
}

// Verifies deduction of timeout's void await-resume type.
TEST_F(future_traits_fixture, resume_type) {
    static_assert(std::same_as<ace::core::meta::resume_type<ace::futures::timeout>, void>);
    SUCCEED();
}
