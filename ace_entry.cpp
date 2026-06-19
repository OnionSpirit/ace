/**
 * @file ace_entry.cpp
 * @brief Flexible mode: weak main() that bootstraps the runtime.
 *
 * Linked when the framework is built with `-Dentry_mode=weak`.
 * The weak `main()` allows the user to override with their own `main()`
 * (traditional mode) or rely on it to call their `co_main()` (zero-boilerplate).
 *
 * If the user defines neither `main()` nor `co_main()`, the linker sees
 * an unresolved reference to `co_main()` (declared non-weak in entry.h)
 * and emits an undefined-symbol error — "neither" detection.
 *
 * If the user defines BOTH `main()` and `co_main()`: the user's `main()`
 * overrides the weak one; `co_main()` is defined but never called → compiles.
 * This is a known limitation of the flexible mode.
 */
#include "ace/ace.h"

namespace {

ace::task co_main_helper(int argc, char** argv, ace::entry_result& out) {
    out = co_await co_main(argc, argv);
    co_return;
}

} // anonymous namespace

ACE_WEAK auto co_main() -> ace::entry {
    std::cerr << "Either 'main()' or 'co_main()' shall be defined\n";
    co_return 126;
}

ACE_WEAK auto co_main(int argc, char** argv) -> ace::entry {
    co_return co_await co_main();
}

ACE_WEAK int main(int argc, char** argv)
{
    ace::entry_result result {};
    ace::cfg::init();
    ace::schedule(co_main_helper(argc, argv, result));
    ace::run();
    return result.code;
}
