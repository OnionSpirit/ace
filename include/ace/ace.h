/**
 * @file ace.h
 * @brief Main public entry point for the ACE framework.
 *
 * @details Include this single header to get access to the full public API:
 *  - @c ace::async<T>   — lazy coroutine type (suspends on creation)
 *  - @c ace::promise<T> — eager coroutine type (runs immediately)
 *  - @c ace::schedule() — submit a task to the global dispatcher
 *  - @c ace::spawn()    — spawn a parallel task inside a running coroutine
 *  - @c ace::run()      — process all scheduled tasks (blocking)
 *  - @c ace::reload()   — reconfigure the balancer
 *  - @c ace::cfg::param — template-based configuration (specialise to override)
 *
 * Synchronization primitives are in their own headers:
 *  - @c ace/futures/channel.h  — MPMC channel
 *  - @c ace/futures/cutex.h    — cooperative userspace mutex
 *  - @c ace/futures/timeout.h  — timer futures
 *
 * @par Minimal example (traditional main)
 * @code{.cpp}
 * #include "ace/ace.h"
 *
 * ace::task hello() {
 *     co_return;
 * }
 *
 * int main() {
 *     ace::schedule(hello());
 *     ace::run();
 * }
 * @endcode
 *
 * @par Zero-boilerplate entry point (co_main)
 * @code{.cpp}
 * #include "ace/ace.h"
 *
 * ace::async<int> co_main(int argc, char** argv) {
 *     ace::console::println("Hello from ACE!");
 *     co_return 0;
 * }
 * @endcode
 *
 * @see ace::async, ace::promise, ace::schedule, ace::run, ace::reload, ace::cfg::init, ace::cfg::update, ace::cfg::g_config
 */

#ifndef ACE_H
#define ACE_H

#include "ace/core/entry.h"
#include "ace/core/compose.h"
#include "ace/futures/spawn.h"
#include "ace/futures/post.h"
#include "ace/futures/reattach.h"
#include "futures/get_runner.h"
#include "futures/roaming.h"

namespace ace {

    /**
     * @brief Spawn a parallel task pinned to the current runner (must be co_awaited).
     * @tparam resume_t         Task result type (@c void for fire-and-forget).
     * @tparam promise_rule_t   Coroutine rule tag (must be spawnable).
     */
    template <typename resume_t = void, template <typename> typename promise_rule_t = core::lazy_rule>
        requires ace::core::is_spawnable_rule<promise_rule_t>
    using spawn          = futures::spawn<resume_t, promise_rule_t>;
    /**
     * @brief Post a parallel task to the front of the current runner's queue.
     * @tparam resume_t         Task result type (@c void for fire-and-forget).
     * @tparam promise_rule_t   Coroutine rule tag (must be spawnable).
     */
    template <typename resume_t = void, template <typename> typename promise_rule_t = core::lazy_rule>
        requires ace::core::is_spawnable_rule<promise_rule_t>
    using post           = futures::post<resume_t, promise_rule_t>;
    /// @brief Enable/disable cross-runner migration for the current task.
    using roaming        = futures::roaming;
    /// @brief Retrieve a pointer to the current runner.
    using get_runner     = futures::get_runner;
    /// @brief Migrate the calling coroutine to a different runner.
    using reattach       = futures::reattach;

}

#endif // ACE_H
