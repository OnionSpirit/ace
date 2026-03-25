/**
 * @file
 * @details This file contains a command_traits and its
 * dispatching concept: is_command.
 * Types are intended to be used to create derived command objects,
 * that will be processed by co_await operator, and allow to promises reach
 * inner options of its handlers within its executional context.
 * command_traits class is full duplication of future_traits, but promise_traits
 * class, declares different handling for each.
 */

#ifndef ACE_COMMAND_H
#define ACE_COMMAND_H

#include "ace/futures/future.h"
#include <memory>

namespace ace::commands {

}

#endif // ACE_COMMAND_H
