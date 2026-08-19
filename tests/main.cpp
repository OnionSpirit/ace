#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// #include "ace/ace.h"
// #include "ace/console.h"
//
// ace::entry co_main(int argc, char** argv) {
//     ::testing::InitGoogleTest(&argc, argv);
//     ace::print("Running co_main");
//     co_return RUN_ALL_TESTS();
// }
