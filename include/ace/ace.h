/**
 * @file ace.h
 * @brief Main public entry point for the ACE framework — quick-start header.
 *
 * @details Include this single header to get access to the core public API:
 *  - @c ace::async<T>   — lazy coroutine type (suspends on creation)
 *  - @c ace::promise<T> — eager coroutine type (runs immediately)
 *  - @c ace::schedule() — submit a task to the global dispatcher
 *  - @c ace::spawn()    — spawn a parallel task inside a running coroutine
 *  - @c ace::run()      — process all scheduled tasks (blocking)
 *  - @c ace::reload()   — reconfigure the balancer
 *  - @c ace::cfg::param — template-based configuration (specialise to override)
 *
 * The extended synchronization and async command primitives live in their own
 * headers:
 *  - @c ace/futures/channel.h  — MPMC channel
 *  - @c ace/futures/cutex.h    — cooperative userspace mutex
 *  - @c ace/futures/timeout.h  — timer futures
 *  - @c ace/futures/polling.h  — low-priority task flag
 *
 * This header defines the @c ACE_H guard. Every @c ace/futures/*.h file and
 * @c ace/console.h re-exports its types under short @c ace::X aliases
 * (@c ace::timeout, @c ace::channel, @c ace::cutex, @c ace::println, ...) only
 * when @c ACE_H is already defined, i.e. when @c ace/ace.h is included
 * <b>before</b> the header. Otherwise the types remain available under their
 * full names (@c ace::futures::X, @c ace::console::X).
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

#endif // ACE_H
