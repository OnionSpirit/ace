#include "environment.h"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <barrier>
#include <numeric>
#include <thread>

#include <ace/futures/spawn.h>
#include <ace/net.h>

#include <nukes/dynamic/regular_freelist.h>
#include <nukes/dynamic/mpmc_queue.h>
#include <nukes/dynamic/mpsc_queue.h>

namespace {

ace::task cutex_capture_racer(ace::cutex& mtx, std::string& count, int max) {
    ace::guard guard(mtx);
    for (int i = 0; i < max; ++i) {
        co_await guard.capture();
        count = std::to_string(std::stoi(count) + 1);
        co_await guard.release();
    }
    co_return;
}

ace::task cutex_sync_racer(ace::cutex& mtx, std::string& count, int max) {
    ace::guard guard(mtx);
    for (int i = 0; i < max; ++i) {
        co_await guard.sync();
        count = std::to_string(std::stoi(count) + 1);
        co_await guard.release();
    }
    co_return;
}

ace::task timer_waiter(std::chrono::milliseconds duration, ace::bus<long>& ch) {
    const auto start = ace::services::clock::current_time();
    co_await ace::timeout(duration);
    const auto end = ace::services::clock::current_time();
    ch << (end - start).count();
    co_return;
}

ace::task spawn_cancel_child(int value, ace::bus<int>& result) {
    co_await ace::timeout(std::chrono::seconds(10));
    result << value;
    co_return;
}

ace::task spawn_cancel_tasks(int count, ace::bus<int>& result) {
    for (int i = 0; i < count; ++i) {
        auto handle = co_await ace::spawn(spawn_cancel_child(i, result));
        handle.cancel();
        co_await handle.join();
    }
    result << 1;
    co_return;
}

ace::task timer_warmup() {
    co_await ace::timeout(1ms);
    co_return;
}

ace::task valued_timer(std::chrono::milliseconds duration, ace::bus<int>& ch) {
    co_await ace::timeout(duration);
    ch << static_cast<int>(duration.count());
    co_return;
}

ace::task channel_producer(int count, ace::bus<int>& ch) {
    for (int i = 0; i < count; ++i)
        ch << i;
    co_return;
}

ace::task channel_consumer(int count, ace::bus<int>& ch) {
    int sum = 0;
    for (int i = 0; i < count; ++i)
        sum += co_await ch.pull();
    if (sum != count * (count - 1) / 2)
        std::cerr << "[bench] channel sum mismatch\n";
    co_return;
}

ace::task spawn_join_child(int value, ace::bus<int>& result) {
    result << value;
    co_return;
}

ace::task spawn_join_tasks(int count, ace::bus<int>& result) {
    for (int i = 0; i < count; ++i) {
        auto handle = co_await ace::spawn(spawn_join_child(i, result));
        if (not co_await handle.join())
            std::cerr << "[bench] spawn join failed\n";
    }
    result << 1;
    co_return;
}

ace::task timeout_waiter(std::chrono::milliseconds duration, ace::bus<int>& ch) {
    co_await ace::timeout(duration);
    ch << 1;
    co_return;
}

ace::task compose_and_tasks(int count) {
    for (int i = 0; i < count; ++i)
        co_await (ace::timeout(0ms) and ace::timeout(0ms));
    co_return;
}

ace::task compose_or_tasks(int count) {
    for (int i = 0; i < count; ++i)
        co_await (ace::timeout(0ms) or ace::timeout(0ms));
    co_return;
}

ace::task trivial_task() {
    co_return;
}

std::size_t legacy_weighted_select(
    const std::vector<double>& velocities,
    const double aggregate_velocity,
    const double probability)
{
    double attractiveness_accumulator = 0.0;
    for (std::size_t runner_id = 0; runner_id < velocities.size(); ++runner_id) {
        attractiveness_accumulator += 1.0 - velocities[runner_id] / aggregate_velocity;
        if (probability <= attractiveness_accumulator)
            return runner_id;
    }
    return velocities.size();
}

ace::task pipe_roundtrip_worker(int read_fd, int write_fd, int count) {
    std::string payload(256, 'x');
    for (int i = 0; i < count; ++i) {
        const int written = co_await ace::io::write_query(
            write_fd, payload.data(), static_cast<unsigned>(payload.size()));
        if (written != static_cast<int>(payload.size()))
            co_return;
        const int read = co_await ace::io::read_query(
            read_fd, payload.data(), static_cast<unsigned>(payload.size()));
        if (read != static_cast<int>(payload.size()))
            co_return;
    }
    co_return;
}

ace::task pending_push_producer(int count, ace::bus<int>& ch) {
    for (int i = 0; i < count; ++i)
        co_await ch.pending_push(i);
    co_return;
}

ace::task pending_push_consumer(int count, ace::bus<int>& ch) {
    int sum = 0;
    for (int i = 0; i < count; ++i)
        sum += co_await ch.pull();
    if (sum != count * (count - 1) / 2)
        std::cerr << "[bench] pending_push sum mismatch\n";
    co_return;
}

ace::task gather_runner(ace::bus<ace::core::runner*>& runners) {
    auto* runner = co_await ace::get_runner();
    runners << runner;
    co_return;
}

ace::task reattach_hopper(
    int count, ace::core::runner* first, ace::core::runner* second) {
    for (int i = 0; i < count; ++i) {
        co_await ace::reattach{first};
        co_await ace::reattach{second};
    }
    co_return;
}

ace::task spawn_fire_forget_child(int value, ace::bus<int>& result) {
    result << value;
    co_return;
}

ace::task spawn_fire_forget_tasks(int count, ace::bus<int>& result) {
    for (int i = 0; i < count; ++i)
        co_await ace::spawn(spawn_fire_forget_child(i, result));
    co_return;
}

ace::automaton<int> automaton_values(int count) {
    for (int i = 0; i < count; ++i)
        co_yield i;
    co_return count;
}

ace::task consume_automaton(int count, ace::bus<int>& ch) {
    auto handle = co_await ace::spawn(automaton_values(count));
    for (int i = 0; i <= count; ++i) {
        if (auto value = co_await handle.ping())
            ch << *value;
    }
    co_return;
}

ace::task expire_waiter(
    ace::services::timepoint_t deadline, ace::bus<int>& ch) {
    co_await ace::expire(deadline);
    ch << 1;
    co_return;
}

ace::task compose_variadic_tasks(int count) {
    for (int i = 0; i < count; ++i) {
        co_await (ace::timeout(0ms)
            and ace::timeout(0ms)
            and ace::timeout(0ms));
        co_await (ace::timeout(0ms)
            or ace::timeout(0ms)
            or ace::timeout(0ms));
    }
    co_return;
}

ace::task idle_connection_link_read(ace::net::connection_link& link) {
    char byte = 0;
    (void)co_await link.read(&byte, 1);
}

ace::task cancel_idle_connection_link_reads(
    std::vector<ace::net::connection_link>& links,
    bool& timer_fired,
    int& canceled_reads)
{
    std::vector<ace::core::async_handle<>> handles;
    handles.reserve(links.size());
    for (auto& link : links)
        handles.emplace_back(co_await ace::spawn(idle_connection_link_read(link)));

    co_await ace::timeout(1ms);
    timer_fired = true;
    for (auto& handle : handles) {
        handle.cancel();
        if (not co_await handle.join())
            ++canceled_reads;
    }
}

} // namespace

// ==========================================================================
// BM1 - cutex_race: multithreaded cooperative mutex contention
// ==========================================================================
// Measures cutex throughput under high contention. Eight runners each perform
// N increments protected by the cutex; the final count verifies atomicity.

static void bm_cutex_race_capture(benchmark::State& state) {
    const int runners = 8;
    const int ops_per_racer = 100000;

    for (auto _ : state) {
        configure_runners(runners);
        ace::cutex mtx;
        std::string shared_cnt {"0"};

        for (int i = 0; i < runners; ++i)
            ace::schedule(cutex_capture_racer(mtx, shared_cnt, ops_per_racer));

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
// BM2 - cutex_race_rescheduling: contention with waiter migration
// ==========================================================================
// Matches BM1 with rescheduling enabled so waiters migrate to the releasing
// runner to improve cache locality.

static void bm_cutex_race_sync(benchmark::State& state) {
    const int runners = 8;
    const int ops_per_racer = 100000;

    for (auto _ : state) {
        configure_runners(runners);
        ace::cutex mtx;
        std::string shared_cnt {"0"};

        for (int i = 0; i < runners; ++i)
            ace::schedule(cutex_sync_racer(mtx, shared_cnt, ops_per_racer));

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
// BM3 - timer_parallel: bulk timers on clock/hierarchical_time_wheel
// ==========================================================================
// Measures hierarchical time wheel scaling by creating timers with varying
// durations on four runners and verifying that every timer delivers a result.

static void bm_timer_parallel(benchmark::State& state) {
    constexpr int runners = 4;
    constexpr long sets_count = 10000;
    constexpr long max_in_set = 500;
    constexpr long set_step = 50;
    constexpr long timers_per_set = max_in_set / set_step; // 10
    constexpr long total_timers = sets_count * timers_per_set; // 100,000

    for (auto _ : state) {
        configure_runners(runners);
        ace::bus<long> ch;

        for (int i = 0; i < sets_count; ++i) {
            for (int q = 0; q < max_in_set; q += set_step) {
                ace::schedule(timer_waiter(std::chrono::milliseconds(q), ch));
            }
        }

        ace::run();
        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after timers");
        }

        std::vector<long> res = fetch(ch);
        if (static_cast<long>(res.size()) != total_timers) {
            state.SkipWithError("Timer count mismatch");
        }

        reset_runners();
    }

    state.SetItemsProcessed(state.iterations() * total_timers);
}
BENCHMARK(bm_timer_parallel)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM4 - spawn_cancel: bulk spawn and cancel
// ==========================================================================
// Verifies that bulk spawn followed by immediate cancellation leaves no
// control blocks, nodes, or routers in the dispatcher.

static void bm_spawn_cancel(benchmark::State& state) {
    const int spawn_count = 100;

    for (auto _ : state) {
        ace::bus<int> result;

        ace::schedule(spawn_cancel_tasks(spawn_count, result));
        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty — possible leak");
        }
    }

    state.SetItemsProcessed(state.iterations() * spawn_count);
}
BENCHMARK(bm_spawn_cancel)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM5 - timer_ordering: timer delivery through clock/hierarchical_time_wheel
// ==========================================================================
// Verifies delivery for the durations used by timer_fixture runner tests.

static void bm_timer_ordering(benchmark::State& state) {
    using namespace std::chrono_literals;

    // Warm up the clock service because the first timeout initializes the
    // service coroutine, hierarchical time wheel, and io_uring ring. The
    // service must run in runner context, hence schedule followed by run.
    ace::schedule(timer_warmup());
    ace::run();

    for (auto _ : state) {
        ace::bus<int> ch;

        ace::schedule(valued_timer(501ms, ch));
        ace::schedule(valued_timer(495ms, ch));
        ace::schedule(valued_timer(450ms, ch));
        ace::schedule(valued_timer(401ms, ch));
        ace::schedule(valued_timer(395ms, ch));
        ace::schedule(valued_timer(350ms, ch));
        ace::schedule(valued_timer(300ms, ch));
        ace::schedule(valued_timer(256ms, ch));
        ace::schedule(valued_timer(250ms, ch));
        ace::schedule(valued_timer(200ms, ch));
        ace::schedule(valued_timer(150ms, ch));
        ace::schedule(valued_timer(100ms, ch));
        ace::schedule(valued_timer(50ms, ch));
        ace::schedule(valued_timer(10ms, ch));
        ace::schedule(valued_timer(0ms, ch));

        ace::run();
        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after timers");
            break;
        }

        // Drain in runner context so channel pull can suspend correctly.
        std::vector<int> res = fetch(ch);
        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after drain");
            break;
        }

        // Check that every timer fired exactly once. Timers sharing a wheel slot
        // wake during one advance in insertion order, so channel values are not
        // guaranteed to be strictly ordered by deadline.
        if (res.size() == 15) {
            for (long d : { 501l, 495l, 450l, 401l, 395l, 350l, 300l, 256l, 250l, 200l, 150l, 100l, 50l, 10l, 0l }) {
                if (std::ranges::find(res, d) == res.end()) {
                    state.SkipWithError("Timer missing from results");
                    break;
                }
            }
        } else {
            state.SkipWithError("Timer count mismatch: expected 15");
        }
    }

    state.SetItemsProcessed(state.iterations() * 15);
}
BENCHMARK(bm_timer_ordering)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM6 - multi_runner_cutex: counter integrity under cutex
// ==========================================================================
// Four runners increment a shared counter under cutex and verify that the final
// value equals runners multiplied by increments per racer.

static void bm_multi_runner_cutex(benchmark::State& state) {
    const int runners = 4;
    const int incs_per_racer = 1000;

    for (auto _ : state) {
        configure_runners(runners);
        ace::cutex mtx;
        std::string counter_str = "0";

        for (int r = 0; r < runners; ++r)
            ace::schedule(cutex_capture_racer(mtx, counter_str, incs_per_racer));

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
// BM7 - channel_push_pull: dyn::bus channel throughput
// ==========================================================================
// A producer pushes N values and a consumer pulls them, measuring the complete
// push-to-pull cycle on one runner.

static void bm_channel_push_pull(benchmark::State& state) {
    constexpr int messages = 100000;

    for (auto _ : state) {
        ace::bus<int> ch;

        // Register the consumer first so pull suspends before values are pushed.
        ace::schedule(channel_consumer(messages, ch));
        ace::schedule(channel_producer(messages, ch));
        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after channel test");
        }
    }

    state.SetItemsProcessed(state.iterations() * messages);
}
BENCHMARK(bm_channel_push_pull)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM8 - spawn_join: spawn and join cycle latency
// ==========================================================================
// Spawns N trivial tasks and joins each handle to measure task creation and
// destruction overhead.

static void bm_spawn_join(benchmark::State& state) {
    constexpr int tasks = 20000;

    for (auto _ : state) {
        ace::bus<int> result;

        ace::schedule(spawn_join_tasks(tasks, result));
        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty — possible leak");
        }
    }

    state.SetItemsProcessed(state.iterations() * tasks);
}
BENCHMARK(bm_spawn_join)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM9 - timeout_short: clock throughput for short timers
// ==========================================================================
// Creates N one-millisecond timers on one runner to measure hierarchical time
// wheel subscribe and release throughput.

static void bm_timeout_short(benchmark::State& state) {
    using namespace std::chrono_literals;
    constexpr int timers = 20000;

    for (auto _ : state) {
        ace::bus<int> ch;

        for (int i = 0; i < timers; ++i)
            ace::schedule(timeout_waiter(1ms, ch));

        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after timers");
        }

        std::vector<int> res = fetch(ch);
        if (static_cast<int>(res.size()) != timers) {
            state.SkipWithError("Timer count mismatch");
        }
    }

    state.SetItemsProcessed(state.iterations() * timers);
}
BENCHMARK(bm_timeout_short)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM10 - compose_and_or: AND/OR composition overhead
// ==========================================================================
// Repeats AND/OR composition of immediate timers to measure observer task and
// router overhead.

static void bm_compose_and(benchmark::State& state) {
    using namespace std::chrono_literals;
    constexpr int compositions = 5000;

    for (auto _ : state) {
        ace::schedule(compose_and_tasks(compositions));
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
        ace::schedule(compose_or_tasks(compositions));
        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after or-composition");
        }
    }

    state.SetItemsProcessed(state.iterations() * compositions);
}
BENCHMARK(bm_compose_or)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM11 - schedule_throughput: dispatcher throughput
// ==========================================================================
// Schedules N trivial tasks and runs until empty to measure the runner's
// attach/yank/release cycle.

static void bm_schedule_throughput(benchmark::State& state) {
    constexpr int tasks = 200000;
    const int runners = static_cast<int>(state.range(0));

    configure_runners(runners);

    for (auto _ : state) {
        for (int i = 0; i < tasks; ++i)
            ace::schedule(trivial_task());

        ace::run();

        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after tasks");
        }
    }

    state.SetItemsProcessed(state.iterations() * tasks);
    reset_runners();
}
BENCHMARK(bm_schedule_throughput)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(64)
    ->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM12 - io_buffer_append: scatter-gather buffer assembly
// ==========================================================================
// Appends chunks and assembles/disassembles a buffer to measure io::buffer
// chunk allocation and msghdr construction. Large iovec arrays use the shared
// arena's transient path.

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

// ==========================================================================
// BM13 - io_buffer_clone: deep copy of a scatter-gather buffer
// ==========================================================================
// Builds a buffer from chunks and clones it to measure allocation while walking
// the iovec chain.

static void bm_io_buffer_clone(benchmark::State& state) {
    constexpr int messages = 20000;
    constexpr int chunks_per_message = 8;

    for (auto _ : state) {
        for (int m = 0; m < messages; ++m) {
            ace::io::buffer buf;
            for (int c = 0; c < chunks_per_message; ++c)
                buf.append("chunk {}", c);
            auto clone = buf.clone();
            if (clone.len() != buf.len())
                state.SkipWithError("clone len mismatch");
        }
    }

    state.SetItemsProcessed(state.iterations() * messages * chunks_per_message);
}
BENCHMARK(bm_io_buffer_clone)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM14 - pipe_io_roundtrip: io_uring write/read cycle on a pipe
// ==========================================================================
// Repeats io::write_query and io::read_query on a pipe pair to measure the full
// kernel_controller submit-to-CQE-to-reattach cycle.

static void bm_pipe_io_roundtrip(benchmark::State& state) {
    constexpr int messages = 20000;

    for (auto _ : state) {
        int fds[2];
        if (pipe(fds) != 0) {
            state.SkipWithError("pipe() failed");
            break;
        }

        ace::schedule(pipe_roundtrip_worker(fds[0], fds[1], messages));
        ace::run();
        if (not ace::empty())
            state.SkipWithError("Dispatcher not empty after io roundtrip");

        close(fds[0]);
        close(fds[1]);
    }

    state.SetItemsProcessed(state.iterations() * messages);
}
BENCHMARK(bm_pipe_io_roundtrip)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM15 - channel_pending_push: asynchronous push with backpressure
// ==========================================================================
// The producer uses pending_push while the consumer pulls values, measuring the
// channel buffer backpressure path through channel_router and pending_push.

static void bm_channel_pending_push(benchmark::State& state) {
    constexpr int messages = 20000;

    for (auto _ : state) {
        ace::bus<int> ch;

        ace::schedule(pending_push_producer(messages, ch));
        ace::schedule(pending_push_consumer(messages, ch));
        ace::run();

        if (not ace::empty())
            state.SkipWithError("Dispatcher not empty after pending_push test");
    }

    state.SetItemsProcessed(state.iterations() * messages);
}
BENCHMARK(bm_channel_pending_push)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM16 - reattach_migration: coroutine migration between runners
// ==========================================================================
// Repeatedly switches a task between two runners with reattach to measure the
// cross-runner node transfer through insert_pool and reattach_router.

static void bm_reattach_migration(benchmark::State& state) {
    constexpr int hops = 20000;

    for (auto _ : state) {
        configure_runners(2);
        ace::bus<ace::core::runner*> rch;

        ace::schedule(gather_runner(rch));
        ace::schedule(gather_runner(rch));
        ace::run();
        auto rs = fetch(rch);
        if (rs.size() != 2 or rs[0] == rs[1]) {
            state.SkipWithError("Could not gather two distinct runners");
            reset_runners();
            continue;
        }

        auto* r0 = rs[0];
        auto* r1 = rs[1];

        ace::schedule(reattach_hopper(hops, r0, r1));
        ace::run();

        if (not ace::empty())
            state.SkipWithError("Dispatcher not empty after reattach test");

        reset_runners();
    }

    state.SetItemsProcessed(state.iterations() * hops * 2);
}
BENCHMARK(bm_reattach_migration)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM17 - spawn_fire_forget: bulk spawn without join
// ==========================================================================
// Spawns N trivial fire-and-forget tasks to measure attach and carrier overhead
// without the join mechanism.

static void bm_spawn_fire_forget(benchmark::State& state) {
    constexpr int tasks = 50000;

    for (auto _ : state) {
        ace::bus<int> result;

        ace::schedule(spawn_fire_forget_tasks(tasks, result));
        ace::run();

        if (not ace::empty())
            state.SkipWithError("Dispatcher not empty — possible leak");
    }

    state.SetItemsProcessed(state.iterations() * tasks);
}
BENCHMARK(bm_spawn_fire_forget)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM18 - automaton_ping: consume co_yield values through ping()
// ==========================================================================
// Each of N automata yields K values consumed through handle.ping(), measuring
// automaton_rule, ping_handler, and yield_waiter overhead.

static void bm_automaton_ping(benchmark::State& state) {
    constexpr int values_per_automaton = 10;
    constexpr int automata = 5000;

    for (auto _ : state) {
        ace::bus<int> ch;

        for (int i = 0; i < automata; ++i)
            ace::schedule(consume_automaton(values_per_automaton, ch));

        ace::run();

        if (not ace::empty())
            state.SkipWithError("Dispatcher not empty after automaton test");
    }

    state.SetItemsProcessed(state.iterations() * automata * (values_per_automaton + 1));
}
BENCHMARK(bm_automaton_ping)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM19 - expire_absolute: timers with absolute deadlines
// ==========================================================================
// Creates N short timers through expire(deadline) to measure the clock's
// absolute-deadline path. Deadlines of 1-20 ms exercise the lowest wheel.

static void bm_expire_absolute(benchmark::State& state) {
    constexpr int timers = 5000;

    for (auto _ : state) {
        ace::bus<int> ch;

        const auto base = ace::services::clock::current_time();
        for (int i = 0; i < timers; ++i)
            ace::schedule(expire_waiter(
                base + std::chrono::milliseconds(i % 20 + 1), ch));

        ace::run();

        if (not ace::empty())
            state.SkipWithError("Dispatcher not empty after expire test");

        auto res = fetch(ch);
        if (static_cast<int>(res.size()) != timers)
            state.SkipWithError("Expire timer count mismatch");
    }

    state.SetItemsProcessed(state.iterations() * timers);
}
BENCHMARK(bm_expire_absolute)->Unit(benchmark::kMillisecond);

// ==========================================================================
// BM20 - compose_variadic: variadic AND/OR composition of 3+ futures
// ==========================================================================
// Composes three immediate timers with AND and OR to measure composed observer
// tasks and cascading cancellation.

static void bm_compose_variadic(benchmark::State& state) {
    using namespace std::chrono_literals;
    constexpr int compositions = 3000;

    for (auto _ : state) {
        ace::schedule(compose_variadic_tasks(compositions));
        ace::run();

        if (not ace::empty())
            state.SkipWithError("Dispatcher not empty after variadic composition");
    }

    state.SetItemsProcessed(state.iterations() * compositions * 6);
}
BENCHMARK(bm_compose_variadic)->Unit(benchmark::kMillisecond);

// ===========================================================================
// BM21 - connection_link_idle_cancel: cancellation responsiveness of idle peers
// ===========================================================================
// Starts one, ten, or one hundred reads whose peers remain idle, then verifies
// that the runner still services a 1 ms timer and cancels every receive.

static void bm_connection_link_idle_cancel(benchmark::State& state) {
    const int connections = static_cast<int>(state.range(0));

    for (auto _ : state) {
        std::vector<int> peers;
        std::vector<ace::net::connection_link> links;
        peers.reserve(connections);
        links.reserve(connections);
        bool timer_fired = false;
        int canceled_reads = 0;
        bool setup_failed = false;

        for (int index = 0; index < connections; ++index) {
            int fds[2] = {-1, -1};
            if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
                setup_failed = true;
                break;
            }
            links.emplace_back(fds[0], false);
            peers.emplace_back(fds[1]);
        }
        if (setup_failed) {
            state.SkipWithError("socketpair setup failed");
            for (const int peer : peers)
                ::close(peer);
            break;
        }

        ace::schedule(cancel_idle_connection_link_reads(links, timer_fired, canceled_reads));
        ace::run();
        if (not timer_fired or canceled_reads != connections)
            state.SkipWithError("idle receive blocked the timer or was not canceled");
        if (not ace::empty())
            state.SkipWithError("dispatcher not empty after idle receive cancellation");

        // Destruction owns the local ends; draining afterward gives their
        // asynchronous guards one normal cleanup pass before the peer closes.
        links.clear();
        ace::run();
        for (const int peer : peers)
            ::close(peer);
    }

    state.SetItemsProcessed(state.iterations() * connections);
}
BENCHMARK(bm_connection_link_idle_cancel)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Unit(benchmark::kMillisecond);

// ===========================================================================
// BM22 - nukes_node_release: local node-pool capture/release throughput
// ===========================================================================
// Measures the hot reuse path through the configured Nukes node allocator.
// The freelist remains alive for the benchmark so only the first capture grows
// the durable arena; all timed iterations reuse the same node storage.

static void bm_nukes_node_release(benchmark::State& state) {
    nukes::dynamic::reg_freelist<std::uint64_t> freelist;

    for (auto _ : state) {
        std::uint64_t* value = nullptr;
        if (not freelist.capture(value)) {
            state.SkipWithError("Nukes node allocation failed");
            break;
        }
        benchmark::DoNotOptimize(value);
        if (not freelist.release(value)) {
            state.SkipWithError("Nukes node release failed");
            break;
        }
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_nukes_node_release)->Unit(benchmark::kNanosecond);

// ===========================================================================
// BM23 - legacy_weighted_selection: isolated old O(N) selection algorithm
// ===========================================================================
// Reproduces the production weighted-selection loop over synthetic equal loads.
// Candidate probabilities are generated outside the measured selection helper,
// so the result isolates the scan and exposes its scaling and index bias.

static void bm_legacy_weighted_selection(benchmark::State& state) {
    const std::size_t runners = static_cast<std::size_t>(state.range(0));
    std::vector<double> velocities(runners, 1.0);
    const double aggregate = std::accumulate(velocities.begin(), velocities.end(), 0.0);
    std::uint64_t sequence = 0x9e3779b97f4a7c15ULL;
    std::array<std::size_t, 64> selections {};

    for (auto _ : state) {
        sequence ^= sequence << 13;
        sequence ^= sequence >> 7;
        sequence ^= sequence << 17;
        const double probability = static_cast<double>(sequence >> 11)
            * (1.0 / static_cast<double>(1ULL << 53));
        std::size_t selected = legacy_weighted_select(
            velocities, aggregate, probability);
        benchmark::DoNotOptimize(selected);
        if (selected < selections.size())
            ++selections[selected];
    }

    std::size_t selected_runners = 0;
    for (std::size_t runner_id = 0; runner_id < runners; ++runner_id)
        selected_runners += selections[runner_id] != 0;
    state.counters["selected_runners"] = static_cast<double>(selected_runners);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_legacy_weighted_selection)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(64)
    ->Unit(benchmark::kNanosecond);

// ===========================================================================
// BM24 - repeated_short_run: dispatcher startup and quiescence overhead
// ===========================================================================
// Measures complete schedule/run cycles for tiny workloads. The zero-task case
// isolates dispatcher startup/shutdown; the other cases show when useful work
// becomes large enough to amortize worker lifecycle and idle waiting.

static void bm_repeated_short_run(benchmark::State& state) {
    const int runners = static_cast<int>(state.range(0));
    const int tasks = static_cast<int>(state.range(1));
    configure_runners(runners);

    for (auto _ : state) {
        for (int task_id = 0; task_id < tasks; ++task_id)
            ace::schedule(trivial_task());
        ace::run();
        if (not ace::empty()) {
            state.SkipWithError("Dispatcher not empty after short run");
            break;
        }
    }

    state.counters["tasks"] = static_cast<double>(tasks);
    state.SetItemsProcessed(state.iterations() * std::max(tasks, 1));
    reset_runners();
}

static void repeated_short_run_args(benchmark::internal::Benchmark* benchmark) {
    for (const int runners : {1, 2, 4, 8, 16})
        for (const int tasks : {0, 1, 10, 100})
            benchmark->Args({runners, tasks});
}

BENCHMARK(bm_repeated_short_run)
    ->Apply(repeated_short_run_args)
    ->Unit(benchmark::kMicrosecond);

// ===========================================================================
// BM25 - dynamic_queue_throughput: concurrent Nukes queue/reclamation cost
// ===========================================================================
// Transfers a fixed batch of unique integers while excluding worker creation
// from the timed interval. The checksum converts loss or duplication into a
// benchmark error instead of reporting throughput for a corrupted run.

template <typename queue_t>
static void bm_dynamic_queue_throughput(benchmark::State& state) {
    constexpr std::uint64_t messages_per_producer = 16'384;
    const auto producer_count = static_cast<std::size_t>(state.range(0));
    const auto consumer_count = static_cast<std::size_t>(state.range(1));
    const auto message_count = producer_count * messages_per_producer;
    const auto expected_sum = message_count * (message_count + 1) / 2;

    for (auto _ : state) {
        state.PauseTiming();
        queue_t queue;
        std::barrier start_gate(
            static_cast<std::ptrdiff_t>(producer_count + consumer_count + 1));
        std::atomic<std::size_t> producers_done {};
        std::atomic<std::size_t> consumed {};
        std::atomic<std::uint64_t> checksum {};
        std::atomic<bool> failed {};
        std::vector<std::thread> workers;
        workers.reserve(producer_count + consumer_count);

        for (std::size_t consumer = 0; consumer < consumer_count; ++consumer) {
            workers.emplace_back([&] {
                start_gate.arrive_and_wait();
                const auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::seconds(10);
                while (consumed.load(std::memory_order_relaxed) < message_count) {
                    std::uint64_t value {};
                    if (queue.pop(value)) {
                        checksum.fetch_add(value, std::memory_order_relaxed);
                        consumed.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    if (producers_done.load(std::memory_order_acquire) == producer_count) {
                        if (std::chrono::steady_clock::now() >= deadline) {
                            failed.store(true, std::memory_order_release);
                            break;
                        }
                    }
                    std::this_thread::yield();
                }
            });
        }

        for (std::size_t producer = 0; producer < producer_count; ++producer) {
            workers.emplace_back([&, producer] {
                start_gate.arrive_and_wait();
                const auto first = producer * messages_per_producer + 1;
                for (std::uint64_t offset = 0; offset < messages_per_producer; ++offset) {
                    auto value = first + offset;
                    while (not queue.push(std::move(value)))
                        std::this_thread::yield();
                }
                producers_done.fetch_add(1, std::memory_order_release);
            });
        }

        state.ResumeTiming();
        start_gate.arrive_and_wait();
        for (auto& worker : workers)
            worker.join();
        state.PauseTiming();

        if (failed.load(std::memory_order_acquire)
            or consumed.load(std::memory_order_relaxed) != message_count
            or checksum.load(std::memory_order_relaxed) != expected_sum) {
            state.SkipWithError("Dynamic queue lost or duplicated messages");
            break;
        }
        state.ResumeTiming();
    }

    state.counters["producers"] = static_cast<double>(producer_count);
    state.counters["consumers"] = static_cast<double>(consumer_count);
    state.SetItemsProcessed(state.iterations() * message_count);
}

static void bm_dynamic_mpsc_queue(benchmark::State& state) {
    bm_dynamic_queue_throughput<nukes::dynamic::mpsc_queue<std::uint64_t>>(state);
}
BENCHMARK(bm_dynamic_mpsc_queue)
    ->Args({1, 1})
    ->Args({4, 1})
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond);

static void bm_dynamic_mpmc_queue(benchmark::State& state) {
    bm_dynamic_queue_throughput<nukes::dynamic::mpmc_queue<std::uint64_t>>(state);
}
BENCHMARK(bm_dynamic_mpmc_queue)
    ->Args({1, 1})
    ->Args({4, 4})
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond);
