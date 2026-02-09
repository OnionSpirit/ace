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

    /**
     * @details Trait class for command objects
     * @tparam derivedT Derived type
     */
    template <typename derivedT>
    struct command_traits : futures::future_handler {

        using derived_command_t = derivedT;

        /**
         * @details Allows
         * use created object with @b co_await
         * @b operator inside context code,
         * and makes derived class @b awaitable
         */
        auto&& operator co_await() {
            return std::move(*static_cast<derived_command_t*>(this));
        }

        bool await_ready() override { return false; };

        /**
         * @details Default destructor
         */
        ~command_traits() override = default;
    };

    #define DECLARE_COMMAND(command_t) typedef ace::commands::command_traits<command_t> command_traits_t;

    #define IMPORT_COMMAND_ENV using typename command_traits_t::derived_command_t;

}

#endif // ACE_COMMAND_H
