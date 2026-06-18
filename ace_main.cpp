#include "ace/ace.h"

ace::task co_main_helper(int argc, char** argv, ace::entry_result& out) {
    out = co_await ::co_main(argc, argv);
    co_return;
}

ACE_WEAK int main(int argc, char** argv)
{
    ace::entry_result result {};
    ace::cfg::init();
    ace::schedule(co_main_helper(argc, argv, result));
    ace::run();
    return result.code;
}

// ACE_WEAK ace::entry co_main() {
//     ACE_MISSING_ENTRYPOINT_ERROR();
//     co_return -1;
// };
//
// ACE_WEAK ace::entry co_main(int, char**) {
//     co_return co_await co_main();
// };
