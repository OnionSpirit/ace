/**
 * @file co_main.h
 * @brief Macro-free coroutine entry point — drop-in replacement for main().
 *
 * @details A forward declaration of @c co_main plus a weak definition of
 * @c int main(int,char**) provide the real entry point.  The weak attribute
 * allows multiple translation units that include this header to coexist
 * (the linker picks one), and also allows the user to override with their
 * own @c main() if they choose not to use @c co_main.
 *
 * @par Usage
 * @code{.cpp}
 * #include <ace/ace.h>
 *
 * ace::async<int> co_main(int argc, char** argv) {
 *     ace::console::println("Hello argc={}", argc);
 *     co_await ace::futures::timeout(500ms);
 *     co_return 0;
 * }
 * @endcode
 *
 * @c co_return provides the process exit code.  The signature matches
 * standard @c main: parameters @c (int argc, char** argv).
 *
 * @see ace::async, ace::schedule, ace::run
 */

#ifndef ACE_CORE_CO_MAIN_H
#define ACE_CORE_CO_MAIN_H

#include "ace/core/async.h"
#include "ace/core/dispatcher.h"

// ---------------------------------------------------------------------------
// Forward declaration — the user's co_main must match this signature
// ---------------------------------------------------------------------------

auto co_main(int argc, char** argv) -> ace::async<int>;

// ---------------------------------------------------------------------------
// Weak main — injected into every TU that includes this header.
//
// On ELF, weak symbols are safe across multiple translation units — the
// linker picks one.  If the user defines a strong main(), the weak one is
// silently overridden.
//
// Define ACE_NO_CO_MAIN before including ace.h to suppress this injection
// (e.g. in test suites or when providing your own main()).
// ---------------------------------------------------------------------------

#if !defined(ACE_NO_CO_MAIN)

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmain"
#endif

__attribute__((weak))
int main(int argc, char** argv)
{
    auto coro = ::co_main(argc, argv);
    int exit_code = 0;

    auto wrapper = [](ace::async<int> inner, int& out) -> ace::task {
        out = co_await inner;
        co_return;
    }(std::move(coro), exit_code);

    ace::schedule(std::move(wrapper));
    ace::run();
    return exit_code;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif // !ACE_NO_CO_MAIN

// ---------------------------------------------------------------------------
// Unit-test helper
// ---------------------------------------------------------------------------

namespace ace::detail {

    /**
     * @brief Runs an @c async<int> coroutine through the ACE event loop
     *        and returns the exit code.  Exposed for testing.
     */
    inline int run_co_main_int(ace::async<int>&& coro) {
        int exit_code = 0;
        auto wrapper = [](ace::async<int> inner, int& out) -> ace::task {
            out = co_await inner;
            co_return;
        }(std::move(coro), exit_code);
        ace::schedule(std::move(wrapper));
        ace::run();
        return exit_code;
    }

} // namespace ace::detail

#endif // ACE_CORE_CO_MAIN_H
