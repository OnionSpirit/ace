/**
 * @file entry.h
 * @brief ACE entry point — supports both `main()` and `co_main()`.
 *
 * @details The behaviour depends on the meson build option `entry_mode`:
 *
 *  `-Dentry_mode=weak` (default — flexible mode):
 *  The library injects a WEAK `main()` that bootstraps the runtime and
 *  calls `co_main()`.  The WEAK attribute lets the user override with
 *  their own `main()` whenever they prefer manual scheduling.
 *
 *  | User defines        | Result          |
 *  |---------------------|-----------------|
 *  | Only `main()`       | OK              |
 *  | Only `co_main()`    | OK              |
 *  | Neither             | link error      |
 *  | Both                | OK (not caught) |
 *
 *  `-Dentry_mode=none` (traditional mode):
 *  The library provides NO `main()`.  The user MUST define `int main()`
 *  themselves.  `co_main()` is still declared but never called.
 *
 * Also contains @c ace::cfg::init() — placed here so that
 * @c detail::resolve<Tag>() is instantiated AFTER the user's
 * @c ace_param<Tag> specialisation (which sits between @c config.h and
 * @c ace.h).
 *
 * @par Usage (weak mode)
 * @code{.cpp}
 * #include <ace/ace.h>
 *
 * ace::async<int> co_main(int argc, char** argv) {
 *     co_await ace::futures::timeout(500ms);
 *     co_return 0;
 * }
 * @endcode
 *
 * @par Usage (none mode)
 * @code{.cpp}
 * #include <ace/ace.h>
 *
 * int main() {
 *     ace::schedule( ... );
 *     ace::run();
 *     return 0;
 * }
 * @endcode
 *
 * @see ace::async, ace::schedule, ace::run, ace::cfg::init
 */

#ifndef ACE_CORE_CO_MAIN_H
#define ACE_CORE_CO_MAIN_H

#include "ace/core/async.h"
#include "ace/core/config.h"

namespace ace::cfg {

    /**
     * @brief One-time initialisation of all configuration parameters.
     *
     * Called once from the injected main() before the dispatcher starts.
     * Sets every known parameter from its compile-time default or user
     * specialisation of @c ace_param<Tag> (picked up via @c detail::resolve).
     *
     * Place here parameters that are LOCKED after startup.
     * @warning To apply user specialisation required include order is: config.h → (user specialisation) → entry.h
     */
    inline void init() {
        g_config._runners_amount = detail::resolve<runners_amount>();

        // [NEW PARAM]:
        // g_config._max_tasks_per_yank = detail::resolve<max_tasks_per_yank>();
    }

} // namespace ace::cfg

// NOTE: Defining of entry result
namespace ace {

    struct entry_result {
        int code = 0;
        entry_result() = default;
        entry_result(const int code)
            : code(code) {}
    };

    using entry = async<entry_result>;
}

/**
 * @brief ACE framework entry point (no-argument overload).
 *
 * Called by the injected `main()` inside the framework library.
 * Must be defined by the user when using @c co_main mode.
 * Declaration is non-weak so that an undefined-reference link error
 * fires when the user forgot to define it ("neither" detection).
 */
auto co_main() -> ace::entry;

/**
 * @brief ACE framework entry point (argc/argv overload).
 *
 * Preferred overload when both `main()` and `co_main()` are used in
 * the same project: the injected `main()` forwards its arguments here.
 * Must be defined by the user when using @c co_main mode.
 * Declaration is non-weak — see `co_main()` above.
 */
auto co_main(int argc, char** argv) -> ace::entry;

#endif // ACE_CORE_CO_MAIN_H
