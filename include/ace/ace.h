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
 *
 * Synchronization primitives are in their own headers:
 *  - @c ace/futures/channel.h  — MPMC channel
 *  - @c ace/futures/cutex.h    — cooperative userspace mutex
 *  - @c ace/futures/timeout.h  — timer futures
 *
 * @par Minimal example
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
 * @see ace::async, ace::promise, ace::schedule, ace::run
 */

#ifndef ACE_H
#define ACE_H

#include "ace/coroutines/context.h"
#include "ace/core/dispatcher.h"

#endif // ACE_H
