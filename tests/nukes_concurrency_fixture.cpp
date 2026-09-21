#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <nukes/dynamic/mpmc_queue.h>
#include <nukes/dynamic/mpsc_queue.h>

namespace {

using namespace std::chrono_literals;

struct nukes_concurrency_fixture : ::testing::Test {};

// Verifies exact-once delivery and per-producer FIFO with four concurrent MPSC producers.
TEST_F(nukes_concurrency_fixture, mpsc_multi_producer_fifo_and_exact_once) {
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t values_per_producer = 20000;
    constexpr std::size_t value_count = producer_count * values_per_producer;
    nukes::dynamic::mpsc_queue<std::uint64_t> queue;
    std::barrier start_gate(static_cast<std::ptrdiff_t>(producer_count + 2));
    std::atomic<bool> timed_out {};
    std::array<std::size_t, producer_count> next_sequence {};
    std::vector<bool> seen(value_count);
    std::thread consumer([&] {
        start_gate.arrive_and_wait();
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        std::size_t consumed = 0;
        while (consumed < value_count) {
            std::uint64_t value {};
            if (queue.pop(value)) {
                ASSERT_LT(value, value_count);
                const auto producer = value / values_per_producer;
                const auto sequence = value % values_per_producer;
                EXPECT_EQ(next_sequence[producer]++, sequence);
                EXPECT_FALSE(seen[value]);
                seen[value] = true;
                ++consumed;
                continue;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                timed_out.store(true, std::memory_order_release);
                break;
            }
            std::this_thread::yield();
        }
    });

    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            start_gate.arrive_and_wait();
            for (std::size_t sequence = 0; sequence < values_per_producer; ++sequence) {
                auto value = static_cast<std::uint64_t>(
                    producer * values_per_producer + sequence);
                while (not queue.push(std::move(value)))
                    std::this_thread::yield();
            }
        });
    }

    start_gate.arrive_and_wait();
    for (auto& producer : producers)
        producer.join();
    consumer.join();

    EXPECT_FALSE(timed_out.load(std::memory_order_acquire));
    for (const bool observed : seen)
        EXPECT_TRUE(observed);
    EXPECT_TRUE(queue.empty());
}

// Verifies exact-once delivery with four concurrent MPMC producers and consumers.
TEST_F(nukes_concurrency_fixture, mpmc_multi_producer_consumer_exact_once) {
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t consumer_count = 4;
    constexpr std::size_t values_per_producer = 20000;
    constexpr std::size_t value_count = producer_count * values_per_producer;
    nukes::dynamic::mpmc_queue<std::uint64_t> queue;
    std::barrier start_gate(
        static_cast<std::ptrdiff_t>(producer_count + consumer_count + 1));
    std::atomic<std::size_t> producers_done {};
    std::atomic<std::size_t> consumed {};
    std::atomic<bool> failed {};
    std::vector<std::atomic<unsigned char>> seen(value_count);
    std::vector<std::thread> workers;
    workers.reserve(producer_count + consumer_count);

    for (std::size_t consumer = 0; consumer < consumer_count; ++consumer) {
        workers.emplace_back([&] {
            start_gate.arrive_and_wait();
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (consumed.load(std::memory_order_relaxed) < value_count) {
                std::uint64_t value {};
                if (queue.pop(value)) {
                    if (value >= value_count) {
                        failed.store(true, std::memory_order_release);
                    } else if (seen[value].exchange(1, std::memory_order_relaxed) != 0) {
                        failed.store(true, std::memory_order_release);
                    }
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
            for (std::size_t sequence = 0; sequence < values_per_producer; ++sequence) {
                auto value = static_cast<std::uint64_t>(
                    producer * values_per_producer + sequence);
                while (not queue.push(std::move(value)))
                    std::this_thread::yield();
            }
            producers_done.fetch_add(1, std::memory_order_release);
        });
    }

    start_gate.arrive_and_wait();
    for (auto& worker : workers)
        worker.join();

    EXPECT_FALSE(failed.load(std::memory_order_acquire));
    EXPECT_EQ(value_count, consumed.load(std::memory_order_relaxed));
    for (const auto& observed : seen)
        EXPECT_EQ(1, observed.load(std::memory_order_relaxed));
    EXPECT_TRUE(queue.empty());
}

// Verifies that a prefilled MPMC queue retains strict FIFO order for one consumer.
TEST_F(nukes_concurrency_fixture, mpmc_prefilled_fifo) {
    nukes::dynamic::mpmc_queue<std::uint64_t> queue;
    constexpr std::uint64_t value_count = 10000;
    for (std::uint64_t value = 0; value < value_count; ++value) {
        auto pushed = value;
        ASSERT_TRUE(queue.push(std::move(pushed)));
    }
    for (std::uint64_t expected = 0; expected < value_count; ++expected) {
        std::uint64_t actual {};
        ASSERT_TRUE(queue.pop(actual));
        EXPECT_EQ(expected, actual);
    }
    EXPECT_TRUE(queue.empty());
}

// Verifies MPSC batch extraction returns every payload in FIFO order and never exposes the dummy.
TEST_F(nukes_concurrency_fixture, mpsc_batch_excludes_dummy_and_drains_snapshot) {
    nukes::dynamic::mpsc_queue<std::uint64_t> queue;
    constexpr std::uint64_t value_count = 10000;
    for (std::uint64_t value = 0; value < value_count; ++value) {
        auto pushed = value;
        ASSERT_TRUE(queue.push(std::move(pushed)));
    }

    std::vector<std::uint64_t> values;
    values.reserve(value_count);
    for (const auto value : queue.pop_batch())
        values.emplace_back(value);

    ASSERT_EQ(value_count, values.size());
    for (std::uint64_t expected = 0; expected < value_count; ++expected)
        EXPECT_EQ(expected, values[expected]);
    EXPECT_TRUE(queue.empty());
}

// Verifies MPMC batch extraction owns an exact FIFO snapshot and leaves the queue reusable.
TEST_F(nukes_concurrency_fixture, mpmc_batch_excludes_dummy_and_reuses_nodes) {
    nukes::dynamic::mpmc_queue<std::uint64_t> queue;
    constexpr std::uint64_t value_count = 10000;
    for (std::uint64_t value = 0; value < value_count; ++value) {
        auto pushed = value;
        ASSERT_TRUE(queue.push(std::move(pushed)));
    }

    std::vector<std::uint64_t> values;
    values.reserve(value_count);
    for (const auto value : queue.pop_batch())
        values.emplace_back(value);

    ASSERT_EQ(value_count, values.size());
    for (std::uint64_t expected = 0; expected < value_count; ++expected)
        EXPECT_EQ(expected, values[expected]);
    EXPECT_TRUE(queue.empty());

    std::uint64_t pushed = value_count;
    ASSERT_TRUE(queue.push(std::move(pushed)));
    std::uint64_t popped {};
    ASSERT_TRUE(queue.pop(popped));
    EXPECT_EQ(value_count, popped);
}

// Verifies external node ownership survives push/pop/release reuse in an MPSC queue.
TEST_F(nukes_concurrency_fixture, mpsc_node_api_reuses_live_storage) {
    nukes::dynamic::mpsc_queue<std::uint64_t> queue;
    using node_t = nukes::dynamic::mpsc_queue<std::uint64_t>::node_t;
    node_t* node = nullptr;
    ASSERT_TRUE(queue._mempool.capture(node));
    node->_data = 42;
    ASSERT_TRUE(queue.push_node(node));

    auto* const popped = queue.pop_node();
    ASSERT_NE(nullptr, popped);
    EXPECT_EQ(42u, popped->_data);
    queue.release_node(popped);
    EXPECT_TRUE(queue.empty());
}

// Verifies move construction transfers all queue nodes and leaves the source inert.
TEST_F(nukes_concurrency_fixture, dynamic_queue_move_transfers_ownership) {
    nukes::dynamic::mpmc_queue<std::uint64_t> source;
    std::uint64_t pushed = 17;
    ASSERT_TRUE(source.push(std::move(pushed)));
    nukes::dynamic::mpmc_queue<std::uint64_t> target(std::move(source));

    std::uint64_t popped {};
    ASSERT_TRUE(target.pop(popped));
    EXPECT_EQ(17u, popped);
    EXPECT_TRUE(target.empty());
}

} // namespace
