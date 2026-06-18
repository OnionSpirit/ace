#include "ace/ace.h"

ace::task co_main_helper(int argc, char** argv, int& out) {
    out = co_await co_main(argc, argv);
    co_return;
}

ACE_WEAK int main(int argc, char** argv)
{
    int exit_code = 0;
    ace::cfg::init();
    ace::schedule(co_main_helper(argc, argv, exit_code));
    ace::run();
    return exit_code;
}

ACE_WEAK ace::entry co_main() {
    ACE_MISSING_ENTRYPOINT_ERROR();
    co_return -1;
};

ACE_WEAK ace::entry co_main(int argc, char** argv) {
    co_return co_await co_main();
};
