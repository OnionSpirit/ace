#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// #include "ace/ace.h"
//
// ace::entry co_main(int argc, char** argv) {
//     ::testing::InitGoogleTest(&argc, argv);
//     co_return RUN_ALL_TESTS();
// }
