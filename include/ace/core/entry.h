/**
 * @file entry.h
 * @brief ACE entry point — drop-in replacement for main().
 *
 * @details A forward declaration of @c co_main plus a weak definition of
 * @c int main(int,char**) provide the real entry point.  The weak attribute
 * allows multiple translation units that include this header to coexist
 * (the linker picks one), and also allows the user to override with their
 * own @c main() if they choose not to use @c co_main.
 *
 * Also contains @c ace::cfg::init() — placed
 * here so that @c detail::resolve<Tag>() is instantiated AFTER the user's
 * @c ace_param<Tag> specialisation (which sits between @c config.h and
 * @c ace.h).
 *
 * @par Usage
 * @code{.cpp}
 * #include <ace/ace.h>
 *
 * ace::async<int> co_main(int argc, char** argv) {
 *     co_await ace::futures::timeout(500ms);
 *     co_return 0;
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

// NOTE: Predefining of entry result
namespace ace { struct entry_result; }

// NOTE: Predefining main from global namespace
// int main(int argc, char** argv);

/**
 * @brief Helper to forbid compiling (linking) without entrypoint
 * @warning IF THIS FUNCTION MENTIONED BY LINKER THEN PROGRAM ENTRYPOINT IS MISSING
 */
extern "C" void ACE_MISSING_ENTRYPOINT_ERROR();

/**
 * @brief Ace framework entry point
*/
ACE_WEAK auto co_main() -> ace::promise<ace::entry_result>;

/**
 * @brief Ace framework entry point
*/
ACE_WEAK auto co_main(int argc, char** argv) -> ace::promise<ace::entry_result>;

namespace ace {

    // inline constexpr bool entry_is_valid =
    //    not ( requires { ::main(0, nullptr); } xor requires { ::co_main(0, nullptr); });

    struct entry_result {
        int code = 0;
        entry_result() = default;
        entry_result(const int code) : code(code) {
            // static_assert(entry_is_valid, "main() and co_main() can not be declared both");
        }
    };

    using entry = promise<entry_result>;
}

#endif // ACE_CORE_CO_MAIN_H
