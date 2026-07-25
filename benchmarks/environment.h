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
#include <ace/console.h>

using namespace std::chrono_literals;

// ==========================================================================
// helpers — общие утилиты для бенчмарков
// ==========================================================================

// Ожидает пока все задачи в dispatcher-е завершатся,
// затем проверяет что состояние чистое.
inline void drain_and_verify() {
    ace::run();
    if (not ace::empty()) {
        ace::console::println("[bench] WARNING: dispatcher not empty after run()");
    }
}

// Сброс конфигурации раннеров в исходное состояние
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

// ==========================================================================
// base_fixture — базовая фикстура с каналом и общими хелперами
// ==========================================================================

struct base_fixture {
    ace::futures::tunnel::dyn::bus<long> channel {};

    template <typename Rep, typename Period>
    static ace::task timer_waiter(std::chrono::duration<Rep, Period> dur,
                                  ace::futures::tunnel::dyn::bus<long>& ch) {
        const auto start = ace::services::clock::current_time();
        co_await ace::futures::timeout(dur);
        const auto end = ace::services::clock::current_time();
        ch << (end - start).count();
        co_return;
    }

    static ace::task channel_fetcher(ace::futures::tunnel::dyn::bus<long>& ch,
                                     std::vector<long>& output) {
        std::vector<long> res {};
        while (not ch.empty()) { res.emplace_back(co_await ch.pull()); }
        output = std::move(res);
        co_return;
    }
};

// ==========================================================================
// cutex_fixture — бенчмарки cooperative mutex
// ==========================================================================

struct cutex_fixture : base_fixture {
    ace::cutex cutex {};

    void TearDown() {
        reset_runners();
    }

    ace::task capture_racer(const int max, std::string& counter) {
        ace::guard crx(cutex);
        for (int i = 0; i < max; ++i) {
            co_await crx.capture();
            counter = std::to_string(std::stoi(counter) + 1);
            crx.release();
        }
        co_return;
    }
};

// ==========================================================================
// timer_parallel_fixture — бенчмарки массовых таймеров
// ==========================================================================

struct timer_parallel_fixture : base_fixture {
    void SetUp(int runners) {
        configure_runners(runners);
    }

    void TearDown() {
        reset_runners();
    }
};

#endif // BENCHMARKS_ENVIRONMENT_H
