#ifndef BENCHMARKS_ENVIRONMENT_H
#define BENCHMARKS_ENVIRONMENT_H

#include <memory>
#include <chrono>
#include <vector>
#include <string>

#include <benchmark/benchmark.h>
#include <ace/ace.h>
#include <ace/futures/channel.h>
#include <ace/futures/timeout.h>
#include <ace/futures/cutex.h>
#include <ace/futures/reattach.h>
#include <ace/futures/get_runner.h>
#include <ace/console.h>

using namespace std::chrono_literals;

static_assert(not is_debug, "benchmarks must be built without debug mode");

// ==========================================================================
// Helpers shared by benchmark scenarios.
// ==========================================================================

// Restore the default single-runner configuration.
inline void reset_runners() {
    ace::cfg::g_config._runners_amount = 1;
    ace::reload();
    ace::reset_signal();
}

// Configure the runner count.
inline void configure_runners(int n) {
    ace::cfg::g_config._runners_amount = n;
    ace::reload();
}

template <typename T>
ace::task fetch_into(ace::bus<T>& ch, std::vector<T>& result) {
    while (not ch.empty())
        result.emplace_back(co_await ch.pull());
    co_return;
}

// Drain a channel through its public API in runner context.
template <typename T>
inline std::vector<T> fetch(ace::bus<T>& ch) {
    std::vector<T> res;
    ace::schedule(fetch_into(ch, res));
    ace::run();
    return res;
}

#endif // BENCHMARKS_ENVIRONMENT_H
