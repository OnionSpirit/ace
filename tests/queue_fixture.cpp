#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <ace/core/tools/queue.h>

namespace tool = ace::core::tools;

struct queue_fixture : ::testing::Test {
    struct test_payload {
        int value;

        explicit test_payload(int v = 0) : value(v) {}
    };

    tool::slab_mempool<test_payload> _mempool {};
    tool::queue<test_payload> _queue {_mempool};
};

struct throwing_payload {
    static inline bool throw_on_copy = false;
    int value {};

    explicit throwing_payload(const int input = 0) noexcept : value(input) {}
    throwing_payload(const throwing_payload& other) : value(other.value) {
        if (throw_on_copy)
            throw std::runtime_error {"copy failure"};
    }
    throwing_payload(throwing_payload&&) noexcept = default;
    ~throwing_payload() noexcept = default;
};

static_assert(not noexcept(std::declval<tool::queue<throwing_payload>&>().enqueue(
    std::declval<const throwing_payload&>())));

// Verifies that a fully returned slab is reused from its original head node.
TEST_F(queue_fixture, slab_mempool_alloc_free) {
    auto* first = _mempool.alloc();
    ASSERT_NE(nullptr, first);

    // A slab has 1024 nodes, so exhausting it makes the free-list order visible.
    std::vector<tool::q_node<test_payload>*> nodes;
    nodes.push_back(first);
    for (int i = 0; i < 1023; ++i) {
        auto* node = _mempool.alloc();
        ASSERT_NE(nullptr, node);
        nodes.push_back(node);
    }
    for (auto* node : nodes)
        _mempool.free(node);

    auto* again = _mempool.alloc();
    ASSERT_NE(nullptr, again);
    EXPECT_EQ(first, again);
    _mempool.free(again);
}

// Verifies that allocation grows beyond the initial 1024-node slab.
TEST_F(queue_fixture, slab_mempool_grow) {
    std::vector<tool::q_node<test_payload>*> nodes;
    for (int i = 0; i < 1025; ++i) {
        auto* node = _mempool.alloc();
        ASSERT_NE(nullptr, node) << "alloc failed at index " << i;
        nodes.push_back(node);
    }
    for (auto* node : nodes)
        _mempool.free(node);
}

// Verifies that destroying a used local pool completes safely.
TEST_F(queue_fixture, slab_mempool_destructor) {
    {
        tool::slab_mempool<test_payload> local_pool;
        auto* node = local_pool.alloc();
        ASSERT_NE(nullptr, node);
        local_pool.free(node);
        for (int i = 0; i < 1025; ++i) {
            auto* extra = local_pool.alloc();
            ASSERT_NE(nullptr, extra);
            local_pool.free(extra);
        }
    }
    SUCCEED();
}

// Verifies that enqueue followed by dequeue preserves FIFO order.
TEST_F(queue_fixture, queue_enqueue_dequeue) {
    _queue.enqueue(test_payload {10});
    _queue.enqueue(test_payload {20});
    _queue.enqueue(test_payload {30});

    // Three values exercise both ends and an interior link.
    EXPECT_EQ(10, _queue.dequeue().value);
    EXPECT_EQ(20, _queue.dequeue().value);
    EXPECT_EQ(30, _queue.dequeue().value);
    EXPECT_TRUE(_queue.empty());
}

// Verifies that the const-reference enqueue overload copies its value.
TEST_F(queue_fixture, queue_enqueue_const_ref) {
    const test_payload value {42};
    _queue.enqueue(value);

    EXPECT_EQ(42, _queue.dequeue().value);
    EXPECT_TRUE(_queue.empty());
}

// Verifies that pop unlinks a node without destroying its payload.
TEST_F(queue_fixture, queue_pop) {
    _queue.enqueue(test_payload {99});
    ASSERT_FALSE(_queue.empty());

    auto&& node = _queue.pop();
    EXPECT_EQ(99, node.data()->value);
    EXPECT_TRUE(_queue.empty());

    // pop transfers cleanup responsibility to the caller.
    node.destruct();
    _mempool.free(&node);
}

// Verifies that removing an interior node repairs its neighboring links.
TEST_F(queue_fixture, queue_remove_node) {
    _queue.enqueue(test_payload {1});
    auto* middle = _queue.enqueue(test_payload {2});
    _queue.enqueue(test_payload {3});

    _queue.remove_node(middle);
    EXPECT_EQ(1, _queue.dequeue().value);
    EXPECT_EQ(3, _queue.dequeue().value);
    EXPECT_TRUE(_queue.empty());
    EXPECT_EQ(nullptr, middle->owning_queue);
    SUCCEED();
}

// Verifies that q_node::remove delegates to its owner and then becomes inert.
TEST_F(queue_fixture, q_node_remove) {
    auto* node = _queue.enqueue(test_payload {77});

    EXPECT_TRUE(node->remove());
    EXPECT_TRUE(_queue.empty());
    // A detached node must reject a second self-removal.
    EXPECT_FALSE(node->remove());
}

// Verifies that moving a queue transfers nodes and empties the source.
TEST_F(queue_fixture, queue_move_constructor) {
    _queue.enqueue(test_payload {55});
    tool::queue<test_payload> moved_queue(std::move(_queue));

    EXPECT_FALSE(moved_queue.empty());
    EXPECT_TRUE(_queue.empty());
    EXPECT_EQ(55, moved_queue.dequeue().value);
    EXPECT_TRUE(moved_queue.empty());
}

// Verifies FIFO order across a longer sequence of enqueued values.
TEST_F(queue_fixture, queue_order) {
    for (int i = 0; i < 10; ++i)
        _queue.enqueue(test_payload {i * 10});

    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(i * 10, _queue.dequeue().value);
    EXPECT_TRUE(_queue.empty());
}

// Verifies that a throwing payload constructor returns its node and leaves the queue usable.
TEST_F(queue_fixture, throwing_payload_rolls_back_enqueue) {
    tool::slab_mempool<throwing_payload> pool;
    tool::queue<throwing_payload> queue(pool);
    const throwing_payload rejected {7};
    throwing_payload::throw_on_copy = true;
    EXPECT_THROW(queue.enqueue(rejected), std::runtime_error);
    EXPECT_TRUE(queue.empty());

    throwing_payload::throw_on_copy = false;
    queue.enqueue(throwing_payload {9});
    EXPECT_EQ(9, queue.dequeue().value);
    EXPECT_TRUE(queue.empty());
}
