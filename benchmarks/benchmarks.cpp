#include "environment.h"

// ==========================================================================
// BM1 — cutex_race: многопоточная гонка на cooperative mutex
// ==========================================================================
// Проверяет пропускную способность cutex при высокой конкуренции.
// 8 раннеров, каждый делает N инкрементов под защитой cutex.
// Счётчик должен быть равен runners * N (атомарность соблюдена).

static void bm_cutex_race_capture(benchmark::State& state) {
    const int runners = 8;
    const int ops_per_racer = 100000;

    for (auto _ : state) {
        configure_runners(runners);
        ace::cutex mtx;
        std::string shared_cnt {"0"};

        // racer: инкрементирует shared_cnt под защитой cutex
        // NOTE: lvalue lambda — rvalue-lambda корутины хранят указатель на
        // оригинальный lambda (GCC), время жизни которого короче корутины.
        auto racer = [](ace::cutex& mtx, std::string& cnt, int max) -> ace::task {
            ace::guard crx(mtx);
            for (volatile int i = 0; i < max; ++i) {
                co_await crx.capture();
                cnt = std::to_string(std::stoi(cnt) + 1);
                co_await crx.release();
            }
            co_return;
        };

        for (int i = 0; i < runners; ++i)
            ace::schedule(racer(mtx, shared_cnt, ops_per_racer));

        ace::run();
        if (std::stoi(shared_cnt) != runners * ops_per_racer) {
            state.SkipWithError("Counter mismatch — cutex atomicity broken");
        }
        reset_runners();
    }

    state.SetItemsProcessed(state.iterations() * runners * ops_per_racer);
}
BENCHMARK(bm_cutex_race_capture)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM2 — cutex_race_rescheduling: гонка с миграцией waiter-ов
// ==========================================================================
// Аналогично BM1 но с включённым rescheduling — waiter'ы мигрируют
// на раннер освободителя для улучшения cache locality.

static void bm_cutex_race_sync(benchmark::State& state) {
    const int runners = 8;
    const int ops_per_racer = 100000;

    for (auto _ : state) {
        configure_runners(runners);
        ace::cutex mtx;
        std::string shared_cnt {"0"};

        auto racer = [](ace::cutex& mtx, std::string& cnt, int max) -> ace::task {
            ace::guard crx(mtx);
            for (volatile int i = 0; i < max; ++i) {
                co_await crx.sync();
                cnt = std::to_string(std::stoi(cnt) + 1);
                co_await crx.release();
            }
            co_return;
        };

        for (int i = 0; i < runners; ++i)
            ace::schedule(racer(mtx, shared_cnt, ops_per_racer));

        ace::run();
        if (std::stoi(shared_cnt) != runners * ops_per_racer) {
            state.SkipWithError("Counter mismatch");
        }
        reset_runners();
    }

    state.SetItemsProcessed(state.iterations() * runners * ops_per_racer);
}
BENCHMARK(bm_cutex_race_sync)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM3 — timer_parallel: массовые таймеры на clock/multi_dial
// ==========================================================================
// Проверяет масштабируемость иерархического колеса времени.
// Создаёт N таймеров с разными duration на 4 раннерах,
// проверяет что все таймеры сработали и данные доставлены.

static void bm_timer_parallel(benchmark::State& state) {
    constexpr int runners = 4;
    constexpr long sets_count = 10000;
    constexpr long max_in_set = 500;
    constexpr long set_step = 50;
    constexpr long timers_per_set = max_in_set / set_step; // 10
    constexpr long total_timers = sets_count * timers_per_set; // 100,000

    for (auto _ : state) {
        configure_runners(runners);
        ace::futures::tunnel::dyn::bus<long> ch;

        // timer_waiter: ждёт duration и пишет elapsed в канал
        auto timer_waiter = [](auto dur, ace::futures::tunnel::dyn::bus<long>& ch) -> ace::task {
            const auto start = ace::services::clock::current_time();
            co_await ace::futures::timeout(dur);
            const auto end = ace::services::clock::current_time();
            ch << (end - start).count();
            co_return;
        };

        for (int i = 0; i < sets_count; ++i) {
            for (int q = 0; q < max_in_set; q += set_step) {
                ace::schedule(timer_waiter(std::chrono::milliseconds(q), ch));
            }
        }

        ace::run();
        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after timers");
        }

        // Дренируем канал и проверяем количество
        std::vector<long> res;
        ace::schedule(base_fixture::channel_fetcher(ch, res));
        ace::run();
        if (static_cast<long>(res.size()) != total_timers) {
            state.SkipWithError("Timer count mismatch");
        }

        reset_runners();
    }

    state.SetItemsProcessed(state.iterations() * total_timers);
}
BENCHMARK(bm_timer_parallel)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM4 — spawn_cancel: массовый spawn + cancel
// ==========================================================================
// Проверяет что при массовом spawn и немедленном cancel нет утечек
// памяти (control_block, ноды, роутеры). Dispatcher должен быть пуст.

static void bm_spawn_cancel(benchmark::State& state) {
    const int spawn_count = 100;

    for (auto _ : state) {
        ace::futures::tunnel::dyn::bus<int> result;

        auto spawner = [](int n, ace::futures::tunnel::dyn::bus<int>& result) -> ace::task {
            for (int i = 0; i < n; ++i) {
                auto handle = co_await ace::spawn([&result, i]() -> ace::task {
                    co_await ace::futures::timeout(std::chrono::seconds(10));
                    int v = i;
                    result << v;
                    co_return;
                }());
                handle.cancel();
                co_await handle.join();
            }
            int v = 1;
            result << v;
            co_return;
        };

        ace::schedule(spawner(spawn_count, result));
        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty — possible leak");
        }
    }

    state.SetItemsProcessed(state.iterations() * spawn_count);
}
BENCHMARK(bm_spawn_cancel)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM5 — timer_ordering: порядок срабатывания таймеров (проверка clock/multi_dial)
// ==========================================================================
// Проверяет что таймеры срабатывают в правильном порядке при разных duration.
// Аналог unit-теста timer_fixture.do_timer_on_runner_test.

static void bm_timer_ordering(benchmark::State& state) {
    using namespace std::chrono_literals;

    // Прогрев clock vortex: первый вызов timeout() инициализирует
    // clock::touch() → spawn vortex → multi_dial. Без прогрева
    // первая итерация бенчмарка может иметь другой тайминг из-за
    // холодного старта инфраструктуры (vortex корутина + io_uring ring).
    // Почему schedule+run а не прямой вызов: vortex должен работать
    // в контексте раннера для корректной инициализации.
    {
        ace::futures::tunnel::dyn::bus<int> warmup_ch;
        auto warmup = [&warmup_ch]() -> ace::task {
            co_await ace::futures::timeout(1ms);
            co_return;
        };
        ace::schedule(warmup());
        ace::run();
    }

    for (auto _ : state) {
        ace::futures::tunnel::dyn::bus<int> ch;

        auto timer_valued = [](auto dur, ace::futures::tunnel::dyn::bus<int>& ch) -> ace::task {
            co_await ace::futures::timeout(dur);
            int v = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()
            );
            ch << v;
            co_return;
        };

        ace::schedule(timer_valued(501ms, ch));
        ace::schedule(timer_valued(500ms, ch));
        ace::schedule(timer_valued(450ms, ch));
        ace::schedule(timer_valued(401ms, ch));
        ace::schedule(timer_valued(400ms, ch));
        ace::schedule(timer_valued(399ms, ch));
        ace::schedule(timer_valued(350ms, ch));
        ace::schedule(timer_valued(300ms, ch));
        ace::schedule(timer_valued(256ms, ch));
        ace::schedule(timer_valued(250ms, ch));
        ace::schedule(timer_valued(200ms, ch));
        ace::schedule(timer_valued(150ms, ch));
        ace::schedule(timer_valued(100ms, ch));
        ace::schedule(timer_valued(50ms, ch));
        ace::schedule(timer_valued(10ms, ch));
        ace::schedule(timer_valued(0ms, ch));

        ace::run();
        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after timers");
            break;
        }

        // Дренируем канал: schedule + run как в оригинальном fetch()
        // Почему schedule + run а не прямой pull: канал наполняется
        // асинхронно таймерами; drainer должен выполняться в контексте
        // раннера чтобы co_await ch.pull() корректно работал.
        std::vector<int> res;
        auto drain = [&ch, &res]() -> ace::task {
            while (not ch.empty()) {
                int v = co_await ch.pull();
                res.push_back(v);
            }
            co_return;
        };
        ace::schedule(drain());
        ace::run();
        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after drain");
            break;
        }

        // Проверяем что значения не убывают
        // Почему нестрогое неравенство: duration 256 и 250 — оба в одном
        // слоте dial, порядок их пробуждения не гарантирован для равных квантов.
        if (res.size() == 16) {
            for (std::size_t i = 1; i < res.size(); ++i) {
                if (res[i] < res[i - 1]) {
                    state.SkipWithError("Timer ordering violation");
                    break;
                }
            }
        } else {
            state.SkipWithError("Timer count mismatch: expected 16");
        }
    }

    state.SetItemsProcessed(state.iterations() * 16);
}
BENCHMARK(bm_timer_ordering)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM6 — multi_runner_cutex: целостность счётчика под cutex
// ==========================================================================
// 4 раннера, каждый инкрементирует счётчик N раз под cutex.
// Проверяет что финальное значение = runners * N.

static void bm_multi_runner_cutex(benchmark::State& state) {
    const int runners = 4;
    const int incs_per_racer = 1000;

    for (auto _ : state) {
        configure_runners(runners);
        ace::cutex mtx;
        std::string counter_str = "0";

        auto racer = [](ace::cutex& mtx, std::string& cnt, int max) -> ace::task {
            auto g = ace::guard(mtx);
            for (int i = 0; i < max; ++i) {
                co_await g.capture();
                cnt = std::to_string(std::stoi(cnt) + 1);
                g.release();
            }
            co_return;
        };

        for (int r = 0; r < runners; ++r)
            ace::schedule(racer(mtx, counter_str, incs_per_racer));

        ace::run();
        if (std::stoi(counter_str) != runners * incs_per_racer) {
            state.SkipWithError("Counter mismatch");
        }
        reset_runners();
    }

    state.SetItemsProcessed(state.iterations() * runners * incs_per_racer);
}
BENCHMARK(bm_multi_runner_cutex)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM7 — channel_push_pull: пропускная способность dyn::bus канала
// ==========================================================================
// Producer пушит N значений, consumer вытягивает их через co_await pull().
// Измеряет полный цикл push→pull в контексте одного раннера.

static void bm_channel_push_pull(benchmark::State& state) {
    constexpr int messages = 100000;

    for (auto _ : state) {
        ace::futures::tunnel::dyn::bus<int> ch;

        auto producer = [](int n, ace::futures::tunnel::dyn::bus<int>& ch) -> ace::task {
            for (int i = 0; i < n; ++i)
                ch << i;
            co_return;
        };

        auto consumer = [](int n, ace::futures::tunnel::dyn::bus<int>& ch) -> ace::task {
            int sum = 0;
            for (int i = 0; i < n; ++i)
                sum += co_await ch.pull();
            if (sum != n * (n - 1) / 2)
                std::cerr << "[bench] channel sum mismatch\n";
            co_return;
        };

        // NOTE: Потребитель должен быть зарегистрирован ДО продюсера —
        // иначе часть push() уйдёт в буфер, и pull() не суспендится.
        ace::schedule(consumer(messages, ch));
        ace::schedule(producer(messages, ch));
        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after channel test");
        }
    }

    state.SetItemsProcessed(state.iterations() * messages);
}
BENCHMARK(bm_channel_push_pull)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM8 — spawn_join: задержка spawn + join цикла
// ==========================================================================
// Спавнит N тривиальных задач и дожидается каждую через handle.join().
// Характеризует накладные расходы на создание/уничтожение задач.

static void bm_spawn_join(benchmark::State& state) {
    constexpr int tasks = 20000;

    for (auto _ : state) {
        ace::futures::tunnel::dyn::bus<int> result;

        auto spawner = [](int n, ace::futures::tunnel::dyn::bus<int>& result) -> ace::task {
            for (int i = 0; i < n; ++i) {
                auto handle = co_await ace::spawn([&result, i]() -> ace::task {
                    int v = i;
                    result << v;
                    co_return;
                }());
                if (not co_await handle.join())
                    std::cerr << "[bench] spawn join failed\n";
            }
            int v = 1;
            result << v;
            co_return;
        };

        ace::schedule(spawner(tasks, result));
        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty — possible leak");
        }
    }

    state.SetItemsProcessed(state.iterations() * tasks);
}
BENCHMARK(bm_spawn_join)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM9 — timeout_short: пропускная способность clock при коротких таймерах
// ==========================================================================
// Создаёт N таймеров по 1ms на 1 раннере — измеряет скорость
// subscribe/release иерархического колеса времени.

static void bm_timeout_short(benchmark::State& state) {
    using namespace std::chrono_literals;
    constexpr int timers = 20000;

    for (auto _ : state) {
        ace::futures::tunnel::dyn::bus<int> ch;

        auto waiter = [](auto dur, ace::futures::tunnel::dyn::bus<int>& ch) -> ace::task {
            co_await ace::futures::timeout(dur);
            int v = 1;
            ch << v;
            co_return;
        };

        for (int i = 0; i < timers; ++i)
            ace::schedule(waiter(1ms, ch));

        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after timers");
        }

        std::vector<int> res;
        auto drain = [&ch, &res]() -> ace::task {
            while (not ch.empty()) {
                int v = co_await ch.pull();
                res.push_back(v);
            }
            co_return;
        };
        ace::schedule(drain());
        ace::run();
        if (static_cast<int>(res.size()) != timers) {
            state.SkipWithError("Timer count mismatch");
        }
    }

    state.SetItemsProcessed(state.iterations() * timers);
}
BENCHMARK(bm_timeout_short)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM10 — compose_and_or: накладные расходы and/or композиций
// ==========================================================================
// N раз co_await (a and b) / (a or b) с мгновенными (0ms) таймерами.
// Характеризует стоимость observer-задач и router-ов.

static void bm_compose_and(benchmark::State& state) {
    using namespace std::chrono_literals;
    constexpr int compositions = 5000;

    for (auto _ : state) {
        auto composer = [](int n) -> ace::task {
            for (int i = 0; i < n; ++i)
                co_await (ace::futures::timeout(0ms) and ace::futures::timeout(0ms));
            co_return;
        };

        ace::schedule(composer(compositions));
        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after and-composition");
        }
    }

    state.SetItemsProcessed(state.iterations() * compositions);
}
BENCHMARK(bm_compose_and)->Unit(benchmark::kMillisecond);

static void bm_compose_or(benchmark::State& state) {
    using namespace std::chrono_literals;
    constexpr int compositions = 5000;

    for (auto _ : state) {
        auto composer = [](int n) -> ace::task {
            for (int i = 0; i < n; ++i)
                co_await (ace::futures::timeout(0ms) or ace::futures::timeout(0ms));
            co_return;
        };

        ace::schedule(composer(compositions));
        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after or-composition");
        }
    }

    state.SetItemsProcessed(state.iterations() * compositions);
}
BENCHMARK(bm_compose_or)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM11 — schedule_throughput: пропускная способность диспетчера
// ==========================================================================
// N тривиальных задач: schedule() + run() до полного опустошения.
// Характеризует стоимость attach/yank/release цикла раннера.

static void bm_schedule_throughput(benchmark::State& state) {
    constexpr int tasks = 200000;

    for (auto _ : state) {
        auto trivial = []() -> ace::task { co_return; };
        for (int i = 0; i < tasks; ++i)
            ace::schedule(trivial());

        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after tasks");
        }
    }

    state.SetItemsProcessed(state.iterations() * tasks);
}
BENCHMARK(bm_schedule_throughput)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM12 — io_buffer_append: сборка scatter-gather буфера
// ==========================================================================
// N раз: append нескольких чанков + assemble() + disassemble() + clear().
// Характеризует аллокацию чанков io::buffer и построение msghdr.
// NOTE: Количество чанков ограничено (iovec_pool лимит — 256 iovec).

static void bm_io_buffer_append(benchmark::State& state) {
    constexpr int messages = 20000;
    constexpr int chunks_per_message = 8;

    for (auto _ : state) {
        for (int m = 0; m < messages; ++m) {
            ace::io::buffer buf;
            for (int c = 0; c < chunks_per_message; ++c)
                buf.append("chunk {}", c);
            const auto* msg = buf.assemble();
            if (msg == nullptr or msg->msg_iovlen != chunks_per_message)
                state.SkipWithError("assemble failed");
            buf.disassemble();
        }
    }

    state.SetItemsProcessed(state.iterations() * messages * chunks_per_message);
}
BENCHMARK(bm_io_buffer_append)->Unit(benchmark::kMillisecond);
