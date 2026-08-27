#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <sys/uio.h>
#include <unistd.h>

#include "environment.h"

#include <ace/futures/get_runner.h>
#include <ace/futures/reattach.h>
#include <ace/futures/timeout.h>
#include <ace/io.h>
#include <ace/net.h>
#include <ace/services/kernelic.h>

namespace {

struct ace_nop_query : ace::io::query<ace_nop_query> {
    IMPORT_IO_QUERY_ENV(ace_nop_query)

    ace_nop_query() : io_query_t(0) {}

    bool setup_query(ace::services::kernel_observer* observer) const noexcept {
        return ace::services::kernel_controller::nop(observer);
    }

    [[nodiscard]] int await_resume() const { return _res; }
};

ace::task run_nop_query(int& result) {
    result = co_await ace_nop_query{};
}

ace::task pipe_write_read(int read_fd, int write_fd) {
    char buffer[64] {};
    const int written = co_await ace::io::write_query(write_fd, "hello pipe", 10);
    EXPECT_EQ(10, written);
    const int read = co_await ace::io::read_query(read_fd, buffer, 10);
    EXPECT_EQ(10, read);
    EXPECT_EQ("hello pipe", std::string_view(buffer, 10));
    ::close(read_fd);
    ::close(write_fd);
}

ace::task pipe_close(int read_fd, int write_fd) {
    const int read_result = co_await ace::io::close_query(read_fd);
    const int write_result = co_await ace::io::close_query(write_fd);
    EXPECT_GE(read_result, 0);
    EXPECT_GE(write_result, 0);
}

ace::task overflow_reader(int read_fd, ace::bus<int>& result) {
    char byte = 0;
    result << co_await ace::io::read_query(read_fd, &byte, 1);
}

ace::task overflow_writer(int write_fd, int readers) {
    co_await ace::timeout(std::chrono::milliseconds(200));
    for (int i = 0; i < readers; ++i)
        (void)::write(write_fd, "x", 1);
}

ace::task register_files_worker() {
    int fds[2] = {-1, -1};
    if (::pipe(fds) == 0) {
        const int nop_result = co_await ace_nop_query{};
        EXPECT_GE(nop_result, 0);
        int registered_fds[2] = {fds[0], fds[1]};
        EXPECT_EQ(0, ace::services::kernel_controller::register_files(registered_fds, 2));
        EXPECT_EQ(1, ace::services::kernel_controller::register_files_update(0, fds[0]));
        EXPECT_EQ(0, ace::services::kernel_controller::unregister_files());
        ::close(fds[0]);
        ::close(fds[1]);
    }
}

template <typename Channel>
ace::task drain_two(Channel& channel, int first, int second) {
    EXPECT_EQ(first, co_await channel.pull());
    EXPECT_EQ(second, co_await channel.pull());
}

ace::task pending_push_value(ace::bus<int, 1>& channel, ace::bus<int>& result) {
    int pushed = 2;
    co_await channel.pending_push(pushed);
    result << 2;
}

ace::task drain_pending_values(ace::bus<int, 1>& channel, ace::bus<int>& result) {
    EXPECT_EQ(1, co_await channel.pull());
    EXPECT_EQ(2, co_await channel.pull());
    result << 1;
}

ace::task mpmc_producer(ace::bus<int>& channel, int producer, int per_producer) {
    for (int i = 0; i < per_producer; ++i)
        channel << (producer * per_producer + i);
    co_return;
}

ace::task mpmc_consumer(ace::bus<int>& channel, ace::bus<int>& result, int count) {
    int sum = 0;
    for (int i = 0; i < count; ++i)
        sum += co_await channel.pull();
    result << sum;
}

ace::task empty_task() {
    co_return;
}

ace::async<int> return_42() {
    co_return 42;
}

ace::task read_async_router_value(ace::bus<int>& result) {
    auto inner = return_42();
    auto handle = inner.observe();
    co_await inner;
    int output = -1;
    EXPECT_TRUE(handle.return_value(&output));
    EXPECT_EQ(42, output);
    result << output;
}

ace::task reattach_current_runner(ace::bus<int>& result) {
    auto* current = co_await ace::get_runner{};
    EXPECT_NE(nullptr, current);
    co_await ace::reattach(current);
    auto* after = co_await ace::get_runner{};
    result << (after == current ? 1 : 0);
}

ace::task reattach_null(ace::bus<int>& result) {
    co_await ace::reattach(nullptr);
    result << 1;
}

ace::task gather_runner(ace::bus<ace::core::runner*>& result) {
    result << co_await ace::get_runner{};
}

ace::task migrate_between_runners(
    ace::bus<int>& result,
    ace::core::runner* first,
    ace::core::runner* second)
{
    co_await ace::reattach(first);
    result << ((co_await ace::get_runner{}) == first ? 1 : 0);
    co_await ace::reattach(second);
    result << ((co_await ace::get_runner{}) == second ? 1 : 0);
}

ace::task udp_server(int port, ace::bus<int>& result) {
    auto socket = co_await ace::net::socket_udp();
    if (not socket) {
        result << 0;
        co_return;
    }
    auto udp = co_await socket.bind("127.0.0.1", static_cast<uint16_t>(port));
    if (not udp) {
        result << 0;
        co_return;
    }
    char buffer[64] {};
    const int received = co_await udp.recv(buffer, sizeof(buffer));
    if (received > 0)
        result << 1;
}

ace::task udp_client(int port, ace::bus<int>& result) {
    auto socket = co_await ace::net::socket_udp();
    if (not socket) {
        result << 0;
        co_return;
    }
    auto local = co_await socket.bind("127.0.0.1", 0);
    if (not local) {
        result << 0;
        co_return;
    }
    auto peer = co_await local.connect("127.0.0.1", static_cast<uint16_t>(port));
    if (not peer) {
        result << 0;
        co_return;
    }
    EXPECT_EQ(8, co_await peer.send("ping-udp"));
}

ace::task udp_bind_ownership(ace::bus<int>& result) {
    auto socket = co_await ace::net::socket_udp();
    if (not socket) {
        result << 0;
        co_return;
    }

    auto bound = co_await socket.bind("127.0.0.1", 0);
    if (not bound) {
        result << 0;
        co_return;
    }
    if (socket) {
        result << 0;
        co_return;
    }

    auto owner = std::move(bound);
    if (bound) {
        result << 0;
        co_return;
    }
    if (not owner) {
        result << 0;
        co_return;
    }

    const int closed = co_await owner.close();
    const int repeated_close = co_await owner.close();
    if (owner) {
        result << 0;
        co_return;
    }
    result << (closed == 0 and repeated_close == 0 ? 1 : 0);
}

ace::task tcp_buffer_server(int port, ace::bus<int>& result) {
    auto socket = co_await ace::net::socket_tcp();
    if (not socket) {
        result << 0;
        co_return;
    }
    auto stream = co_await socket.bind("127.0.0.1", static_cast<uint16_t>(port));
    if (not stream) {
        result << 0;
        co_return;
    }
    auto listener = co_await stream.listen();
    if (not listener) {
        result << 0;
        co_return;
    }
    auto connection = co_await listener.accept("127.0.0.1", 0);
    if (not connection) {
        result << 0;
        co_return;
    }
    ace::io::buffer buffer;
    buffer.expand(64);
    const int received = co_await connection.recv(buffer);
    if (received > 0) {
        const auto message = buffer.as<std::string>().substr(0, static_cast<std::size_t>(received));
        EXPECT_EQ("msg-buffer", message);
        result << 1;
    }
}

ace::task tcp_buffer_client(int port, ace::bus<int>& result) {
    auto socket = co_await ace::net::socket_tcp();
    if (not socket) {
        result << 0;
        co_return;
    }
    auto stream = co_await socket.bind("127.0.0.1", 0);
    if (not stream) {
        result << 0;
        co_return;
    }
    auto connection = co_await stream.connect("127.0.0.1", static_cast<uint16_t>(port));
    if (not connection) {
        result << 0;
        co_return;
    }
    ace::io::buffer buffer;
    buffer.append("msg-buffer");
    if (co_await connection.send(buffer) == 10)
        result << 1;
}

// Verifies nop completion when io_uring is available, otherwise its init error contract.
TEST_F(base_fixture, kernel_controller_nop) {
    if (not ace::services::kernel_controller::available()) {
        // Sandboxed kernels can deny io_uring.  The regression is the exact
        // negative init result, rather than a null-ring crash during submit.
        EXPECT_LT(ace::services::kernel_controller::initialization_error(), 0);
        return;
    }

    int result = 0;
    ace::schedule(run_nop_query(result));
    ace::run();
    EXPECT_TRUE(ace::empty());
    EXPECT_GE(result, 0);
}

// Verifies binary write_query and read_query operations on a local pipe.
TEST_F(base_fixture, io_query_pipe_write_read) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(fds));
    ace::schedule(pipe_write_read(fds[0], fds[1]));
    ace::run();
    EXPECT_TRUE(ace::empty());
}

// Verifies that close_query asynchronously closes both ends of a pipe.
TEST_F(base_fixture, io_query_pipe_close) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(fds));
    ace::schedule(pipe_close(fds[0], fds[1]));
    ace::run();
    EXPECT_TRUE(ace::empty());
}

// Verifies that requests beyond io_uring capacity survive overflow buffering.
TEST_F(base_fixture, kernelic_overflow_buffer_stress) {
    constexpr int readers = 6000;
    int fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(fds));
    ace::bus<int> result;
    for (int i = 0; i < readers; ++i)
        ace::schedule(overflow_reader(fds[0], result));
    ace::schedule(overflow_writer(fds[1], readers));

    ace::run();
    EXPECT_TRUE(ace::empty());
    const auto values = fetch(result);
    // 6000 exceeds the 4096-entry ring and therefore exercises deferred SQEs.
    ASSERT_EQ(static_cast<std::size_t>(readers), values.size());
    ::close(fds[0]);
    ::close(fds[1]);
}

// Verifies the public arena-backed iovec API for pooled and transient sizes.
TEST_F(base_fixture, kernel_iovec_arena_api) {
    ::iovec* small = ace::services::kernel_controller::iovec_allocate(64);
    ASSERT_NE(nullptr, small);
    EXPECT_EQ(64u, small->iov_len);
    EXPECT_NE(nullptr, small->iov_base);
    ace::services::kernel_controller::iovec_deallocate(small);

    ::iovec* big = ace::services::kernel_controller::iovec_allocate(5000);
    ASSERT_NE(nullptr, big);
    EXPECT_EQ(5000u, big->iov_len);
    ace::services::kernel_controller::iovec_deallocate(big);
    ace::services::kernel_controller::iovec_deallocate(nullptr);

    ::iovec* array = ace::services::kernel_controller::iovec_pool_allocate(4);
    ASSERT_NE(nullptr, array);
    ace::services::kernel_controller::iovec_pool_deallocate(array, 4);

    // 300 iovecs exceed 4096 bytes and must use the transient arena path.
    ::iovec* large_array = ace::services::kernel_controller::iovec_pool_allocate(300);
    ASSERT_NE(nullptr, large_array);
    ace::services::kernel_controller::iovec_pool_deallocate(large_array, 300);
    ace::services::kernel_controller::iovec_pool_deallocate(nullptr, 0);
}

// Verifies registration, update, and removal of fixed files on an initialized ring.
TEST_F(base_fixture, kernel_register_files) {
    ace::schedule(register_files_worker());
    ace::run();
    EXPECT_TRUE(ace::empty());
}

// Verifies that a fixed-capacity channel rejects a push when full.
TEST_F(base_fixture, channel_bounded_full) {
    ace::bus<int, 2> channel;
    EXPECT_TRUE(channel.push(1));
    EXPECT_TRUE(channel.push(2));
    EXPECT_FALSE(channel.push(3));
    ace::schedule(drain_two(channel, 1, 2));
    ace::run();
    EXPECT_TRUE(ace::empty());
}

// Verifies that pending_push waits until a full channel gains capacity.
TEST_F(base_fixture, channel_pending_push_waits) {
    ace::bus<int, 1> channel;
    ace::bus<int> result;
    channel.push(1);
    ace::schedule(pending_push_value(channel, result));
    ace::schedule(drain_pending_values(channel, result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    // Both tasks report only after the pending value has crossed the channel.
    ASSERT_EQ(2u, values.size());
}

// Verifies ordered push and pull through the SPSC bridge specialization.
TEST_F(base_fixture, channel_spsc_bridge) {
    ace::bridge<int> channel;
    channel.push(10);
    channel.push(20);
    ace::schedule(drain_two(channel, 10, 20));
    ace::run();
    EXPECT_TRUE(ace::empty());
}

// Verifies lossless MPMC delivery from four producers to one consumer.
TEST_F(base_fixture, channel_mpmc_parallel) {
    constexpr int producers = 4;
    constexpr int per_producer = 100;
    ace::bus<int> channel;
    ace::bus<int> result;
    for (int producer = 0; producer < producers; ++producer)
        ace::schedule(mpmc_producer(channel, producer, per_producer));
    ace::schedule(mpmc_consumer(channel, result, producers * per_producer));

    ace::run();
    EXPECT_TRUE(ace::empty());
    const auto values = fetch(result);
    ASSERT_GE(values.size(), 1u);
    // Every integer in [0, 399] must be delivered exactly once.
    EXPECT_EQ(399 * 400 / 2, values[0]);
}

// Verifies that a coroutine created outside a runner has no current pool.
TEST_F(base_fixture, get_current_pool_outside_runner) {
    auto task = empty_task();
    auto* pool = task._coroutine.promise()._runner.as<ace::runner_pool_t>();
    EXPECT_EQ(nullptr, pool);
}

// Verifies that async_router exposes a completed typed return value.
TEST_F(base_fixture, async_router_return_value) {
    ace::bus<int> result;
    ace::schedule(read_async_router_value(result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(42, values[0]);
}

// Verifies the same-runner reattach path resumes on its requested runner.
TEST_F(base_fixture, reattach_resumes_on_other_runner) {
    ace::bus<int> result;
    ace::schedule(reattach_current_runner(result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_GE(values.size(), 1u);
    EXPECT_EQ(1, values[0]);
}

// Verifies that reattach(nullptr) is a ready no-op.
TEST_F(base_fixture, reattach_nullptr_noop) {
    ace::bus<int> result;
    ace::schedule(reattach_null(result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(1, values[0]);
}

// Verifies explicit migration between two distinct runners.
TEST_F(base_fixture, reattach_cross_runner_migration) {
    ace::cfg::g_config._runners_amount = 2;
    ace::reload();

    ace::bus<ace::core::runner*> runner_channel;
    ace::schedule(gather_runner(runner_channel));
    ace::schedule(gather_runner(runner_channel));
    ace::run();
    const auto runners = fetch(runner_channel);
    ASSERT_EQ(2u, runners.size());
    ASSERT_NE(runners[0], runners[1]);

    ace::bus<int> result;
    ace::schedule(migrate_between_runners(result, runners[0], runners[1]));
    ace::run();
    EXPECT_TRUE(ace::empty());
    const auto values = fetch(result);
    ASSERT_EQ(2u, values.size());
    // Each marker is emitted only after get_runner observes the requested target.
    EXPECT_EQ(1, values[0]);
    EXPECT_EQ(1, values[1]);

    ace::cfg::g_config._runners_amount = 1;
    ace::reload();
}

// Verifies a loopback UDP send and receive through the connected datagram path.
TEST_F(base_fixture, udp_sendto_recv_loop) {
    const int port = 23000 + (static_cast<int>(::getpid()) % 1000);
    ace::bus<int> result;
    ace::schedule(udp_server(port, result));
    ace::schedule(udp_client(port, result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_GE(values.size(), 1u);
    EXPECT_EQ(1, values[0]);
}

// Verifies that UDP bind and move each transfer sole FD ownership exactly once.
TEST_F(base_fixture, udp_bind_transfers_sole_ownership) {
    ace::bus<int> result;
    ace::schedule(udp_bind_ownership(result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_EQ(1u, values.size());
    // The helper checks both moved-from entities and idempotent public close().
    EXPECT_EQ(1, values[0]);
}

// Verifies TCP send(io::buffer) and recv(io::buffer) through sendmsg/recvmsg.
TEST_F(base_fixture, tcp_sendmsg_recvmsg_echo) {
    const int port = 24000 + (static_cast<int>(::getpid()) % 1000);
    ace::bus<int> result;
    ace::schedule(tcp_buffer_server(port, result));
    ace::schedule(tcp_buffer_client(port, result));
    ace::run();
    EXPECT_TRUE(ace::empty());

    const auto values = fetch(result);
    ASSERT_GE(values.size(), 1u);
    EXPECT_EQ(1, values[0]);
}

} // namespace
