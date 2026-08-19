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

// ==========================================================================
// helpers — общие утилиты для бенчмарков
// ==========================================================================

// Сброс конфигурации раннеров в исходное состояние (1 раннер)
inline void reset_runners() {
    ace::cfg::g_config._runners_amount = 1;
    ace::reload();
    ace::reset_signal();
}

// Настройка количества раннеров
inline void configure_runners(int n) {
    ace::cfg::g_config._runners_amount = n;
    ace::reload();
}

// Дренирует канал через публичный API (schedule + run)
template <typename T>
inline std::vector<T> fetch(ace::bus<T>& ch) {
    std::vector<T> res;
    ace::schedule([&ch, &res]() -> ace::task {
        while (not ch.empty())
            res.emplace_back(co_await ch.pull());
        co_return;
    }());
    ace::run();
    return res;
}

#endif // BENCHMARKS_ENVIRONMENT_H
