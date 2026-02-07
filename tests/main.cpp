#include <gtest/gtest.h>

#include "ace/core/balancer.h"
#include "ace/core/clock.h"

int main(int argc, char** argv) {
    // ace::core::s_balancer_config._runners_amount = 4;
    // ace::core::clock::get_instance().enable_multithreading();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
