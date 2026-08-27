#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <ace/ace.h>
#include <ace/fs.h>
#include <ace/futures/channel.h>
#include <ace/futures/get_runner.h>
#include <ace/futures/reattach.h>
#include <ace/futures/spawn.h>
#include <ace/futures/timeout.h>
#include <ace/io.h>
#include <ace/net.h>

struct test_io_entity : ace::io::entity<test_io_entity> {
    IMPORT_IO_ENTITY_ENV(test_io_entity)
    IMPORT_IO_ENTITY_FABRICATION
};

struct test_kernel_observer final : ace::services::kernel_observer {
    void on_result(const int) override {}
};

namespace {

int reject_kernel_queue_init(unsigned, io_uring*, io_uring_params*) {
    return -EPERM;
}

std::atomic<int> outcast_init_failure {INT_MIN};

void record_outcast_init_failure(const int error, const std::span<const char>&) {
    outcast_init_failure.store(error, std::memory_order_relaxed);
}

struct kernel_init_override final {
    explicit kernel_init_override(int (*initializer)(unsigned, io_uring*, io_uring_params*)) {
        ace::services::kernel_controller::set_queue_init_for_testing(initializer);
    }

    kernel_init_override(const kernel_init_override&) = delete;
    kernel_init_override& operator=(const kernel_init_override&) = delete;

    ~kernel_init_override() {
        ace::services::kernel_controller::set_queue_init_for_testing(nullptr);
    }
};

struct outcast_handler_override final {
    using handler_t = decltype(ace::io::outcast::fail_cb_handler);

    explicit outcast_handler_override(const handler_t handler)
        : _previous(ace::io::outcast::fail_cb_handler) {
        ace::io::outcast::fail_cb_handler = handler;
    }

    outcast_handler_override(const outcast_handler_override&) = delete;
    outcast_handler_override& operator=(const outcast_handler_override&) = delete;

    ~outcast_handler_override() {
        ace::io::outcast::fail_cb_handler = _previous;
    }

private:
    handler_t _previous;
};

} // namespace

struct io_entity_fixture : ::testing::Test {
    struct exact_read_storage {
        std::array<unsigned char, 4> bytes {0xCC, 0xCC, 0xCC, 0xCC};
        unsigned char canary = 0xA5;
    };

    static ace::task read_exact(const int fd, exact_read_storage& storage, int& result) {
        result = co_await ace::io::read_query(fd, storage.bytes.data(), storage.bytes.size());
    }

    static ace::task read_after_kernel_init_failure(const int fd, int& result) {
        char byte = 0;
        result = co_await ace::io::read_query(fd, &byte, 1);
    }

    static ace::task write_file_link(ace::fs::file_link& link, const std::string_view payload, const int repeats = 1) {
        for (int i = 0; i < repeats; ++i)
            link.write(std::string_view {payload});
        co_return;
    }

    static ace::task await_close(test_io_entity& entity, int& result) {
        result = co_await entity.close();
    }

    static ace::task await_close_twice(
        test_io_entity& entity,
        int& first_result,
        int& second_result)
    {
        first_result = co_await entity.close();
        second_result = co_await entity.close();
    }

    static ace::task receive_vector(
        ace::net::connection& connection,
        std::vector<char>& buffer,
        int& result)
    {
        result = co_await connection.recv(buffer);
    }

    static ace::task receive_string(
        ace::net::connection& connection,
        std::string& buffer,
        int& result)
    {
        result = co_await connection.recv(buffer);
    }

    static ace::task await_oversize_queries(
        const int read_fd,
        const int write_fd,
        const int socket_fd,
        char& byte,
        int& read_result,
        int& write_result,
        int& send_result,
        int& sendto_result,
        int& recv_result)
    {
        const auto oversize = static_cast<std::size_t>(UINT_MAX) + 1U;
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        read_result = co_await ace::io::read_query(read_fd, &byte, oversize);
        write_result = co_await ace::io::write_query(write_fd, &byte, oversize);
        send_result = co_await ace::net::send_query(socket_fd, &byte, oversize);
        sendto_result = co_await ace::net::net_interface::sendto_query(
            socket_fd,
            &byte,
            oversize,
            0,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address));
        recv_result = co_await ace::net::recv_query(socket_fd, &byte, oversize);
    }

    static ace::task stalled_link_read(
        ace::net::connection_link& link,
        bool& entered,
        bool& completed)
    {
        char byte = 0;
        entered = true;
        (void)co_await link.read(&byte, 1);
        completed = true;
    }

    static ace::task cancel_stalled_link_read(
        ace::net::connection_link& link,
        bool& entered,
        bool& timer_fired,
        bool& read_completed,
        bool& join_failed)
    {
        auto handle = co_await ace::spawn(stalled_link_read(link, entered, read_completed));
        co_await ace::timeout(std::chrono::milliseconds(10));
        timer_fired = true;
        handle.cancel();
        join_failed = not co_await handle.join();
    }

    static ace::task read_link(
        ace::net::connection_link& link,
        char *const data,
        const std::size_t len,
        int& result)
    {
        result = co_await link.read(data, len);
    }

    static ace::task gather_runner(ace::bus<ace::core::runner*>& result) {
        result << co_await ace::get_runner{};
    }

    static ace::task read_link_after_migration(
        ace::net::connection_link& link,
        ace::core::runner *const first,
        ace::core::runner *const second,
        ace::bus<int>& result)
    {
        co_await ace::reattach(first);
        result << ((co_await ace::get_runner{}) == first ? 1 : 0);
        co_await ace::reattach(second);
        result << ((co_await ace::get_runner{}) == second ? 1 : 0);

        char byte = 0;
        const int read_result = co_await link.read(&byte, 1);
        result << (read_result == 1 ? 1 : 0);
        result << ((co_await ace::get_runner{}) == second ? 1 : 0);
    }

    template <typename value_t>
    static ace::task drain_channel(ace::bus<value_t>& channel, std::vector<value_t>& values) {
        while (not channel.empty())
            values.emplace_back(co_await channel.pull());
    }

    template <typename value_t>
    static std::vector<value_t> fetch(ace::bus<value_t>& channel) {
        std::vector<value_t> values;
        ace::schedule(drain_channel(channel, values));
        run_dispatcher();
        return values;
    }

    static void run_dispatcher() {
        ace::run();
        EXPECT_TRUE(ace::empty());
    }

    static void expect_fd_open(const int fd) {
        EXPECT_NE(-1, ::fcntl(fd, F_GETFD));
    }

    static void expect_fd_closed(const int fd) {
        errno = 0;
        EXPECT_EQ(-1, ::fcntl(fd, F_GETFD));
        EXPECT_EQ(EBADF, errno);
    }

    void TearDown() override {
        ace::cfg::g_config._runners_amount = 1;
        ace::reload();
        ace::reset_signal();
    }
};

// Verifies that a default entity is closed and has no valid descriptor.
TEST_F(io_entity_fixture, entity_default_construction) {
    test_io_entity entity;
    EXPECT_TRUE(entity.is_closed());
    EXPECT_FALSE(entity);
}

// Verifies that the parameterized entity constructor preserves its closed state.
TEST_F(io_entity_fixture, entity_param_construction) {
    // A closed state avoids assigning ownership of the synthetic descriptor.
    test_io_entity entity(42, true);
    EXPECT_TRUE(entity.is_closed());
}

// Verifies that move construction transfers the live descriptor and invalidates the source.
TEST_F(io_entity_fixture, entity_move) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    const int owned_fd = pipe_fds[0];

    test_io_entity source(owned_fd, false);
    test_io_entity destination(std::move(source));
    EXPECT_TRUE(source.is_closed());
    EXPECT_FALSE(source);
    EXPECT_FALSE(destination.is_closed());
    EXPECT_TRUE(destination);
    expect_fd_open(owned_fd);

    { auto close = destination.close(); }
    run_dispatcher();
    expect_fd_closed(owned_fd);
    ::close(pipe_fds[1]);
}

// Verifies that extract() returns the descriptor and invalidates its former owner.
TEST_F(io_entity_fixture, entity_extract) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));

    test_io_entity entity(pipe_fds[0], false);
    auto [fd, closed] = entity.extract();
    EXPECT_EQ(pipe_fds[0], fd);
    EXPECT_FALSE(closed);
    EXPECT_TRUE(entity.is_closed());
    EXPECT_FALSE(entity);
    expect_fd_open(fd);

    ::close(fd);
    ::close(pipe_fds[1]);
}

// Verifies that close() immediately invalidates the entity before async dispatch.
TEST_F(io_entity_fixture, entity_close) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    const int owned_fd = pipe_fds[0];

    {
        test_io_entity entity(owned_fd, false);
        auto close = entity.close();
        EXPECT_TRUE(entity.is_closed());
        EXPECT_FALSE(entity);

        // The query now owns the still-open descriptor; dropping it below must
        // dispatch the close rather than letting the entity guard close it.
        expect_fd_open(owned_fd);
    }
    run_dispatcher();
    expect_fd_closed(owned_fd);
    ::close(pipe_fds[1]);
}

// Verifies is_closed() for both explicit and default closed states.
TEST_F(io_entity_fixture, entity_is_closed) {
    test_io_entity explicitly_closed(26, true);
    EXPECT_TRUE(explicitly_closed.is_closed());

    test_io_entity default_entity;
    EXPECT_TRUE(default_entity.is_closed());
}

// Verifies that destroying an invalid entity schedules no close operation.
TEST_F(io_entity_fixture, entity_guard_no_runner) {
    { test_io_entity entity; }
    EXPECT_TRUE(ace::empty());
}

// Verifies that a live entity guard closes its descriptor outside a runner context.
TEST_F(io_entity_fixture, guard_valid_fd_no_runner) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    const int owned_fd = pipe_fds[0];

    { test_io_entity entity(owned_fd, false); }
    run_dispatcher();
    expect_fd_closed(owned_fd);
    ::close(pipe_fds[1]);
}

// Verifies that a guard marked closed does not close the descriptor again.
TEST_F(io_entity_fixture, guard_already_closed) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));

    { test_io_entity entity(pipe_fds[0], true); }
    expect_fd_open(pipe_fds[0]);
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

// Verifies that an exact-size binary read never writes a trailing NUL over a canary.
TEST_F(io_entity_fixture, read_query_exact_buffer_preserves_canary_and_binary_data) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    const std::array<unsigned char, 4> payload = {0x41, 0x00, 0xFF, 0x42};
    ASSERT_EQ(
        static_cast<ssize_t>(payload.size()),
        ::write(pipe_fds[1], payload.data(), payload.size()));

    exact_read_storage storage;
    int result = INT_MIN;
    ace::schedule(read_exact(pipe_fds[0], storage, result));
    run_dispatcher();

    EXPECT_EQ(static_cast<int>(payload.size()), result);
    EXPECT_EQ(payload, storage.bytes);
    // A canary adjacent to an exact-size destination detects the historical
    // _buf[_res] terminator write while the embedded NUL proves binary safety.
    EXPECT_EQ(0xA5, storage.canary);
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

// Verifies that query construction preserves the full io_uring length boundary.
TEST_F(io_entity_fixture, io_query_lengths_preserve_uint_max_boundary) {
    char byte = 0;
    constexpr auto max_length = static_cast<std::size_t>(UINT_MAX);
    constexpr auto almost_max_length = max_length - 1U;
    sockaddr_in address {};
    address.sin_family = AF_INET;

    ace::io::read_query read(0, &byte, max_length);
    ace::io::read_query almost_max_read(0, &byte, almost_max_length);
    ace::io::write_query write(1, &byte, max_length);
    ace::net::send_query send(2, &byte, max_length);
    ace::net::recv_query recv(3, &byte, max_length);
    ace::net::net_interface::sendto_query sendto(
        4,
        &byte,
        max_length,
        0,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address));

    // These constructors do not submit I/O, so a one-byte object safely proves
    // the public query representation reaches UINT_MAX without truncation.
    EXPECT_EQ(max_length, read._nbytes);
    EXPECT_EQ(almost_max_length, almost_max_read._nbytes);
    EXPECT_EQ(max_length, write._nbytes);
    EXPECT_EQ(max_length, send._len);
    EXPECT_EQ(max_length, recv._len);
    EXPECT_EQ(max_length, sendto._len);
    EXPECT_TRUE(ace::services::kernel_controller::is_io_length_supported(max_length));
    EXPECT_TRUE(ace::services::kernel_controller::is_io_length_supported(almost_max_length));
    EXPECT_FALSE(ace::services::kernel_controller::is_io_length_supported(max_length + 1U));
}

// Verifies that every raw read/write/send/receive query reports oversize input asynchronously.
TEST_F(io_entity_fixture, oversize_io_queries_return_eoverflow_without_submission) {
    int pipe_fds[2] = {-1, -1};
    int socket_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds));
    char byte = 0;
    int read_result = INT_MIN;
    int write_result = INT_MIN;
    int send_result = INT_MIN;
    int sendto_result = INT_MIN;
    int recv_result = INT_MIN;

    ace::schedule(await_oversize_queries(
        pipe_fds[0],
        pipe_fds[1],
        socket_fds[0],
        byte,
        read_result,
        write_result,
        send_result,
        sendto_result,
        recv_result));
    run_dispatcher();

    // A common negative errno result is observable without allocating or
    // submitting a gigantic buffer; any submission would instead require the
    // caller's one-byte storage to remain valid for kernel access.
    EXPECT_EQ(-EOVERFLOW, read_result);
    EXPECT_EQ(-EOVERFLOW, write_result);
    EXPECT_EQ(-EOVERFLOW, send_result);
    EXPECT_EQ(-EOVERFLOW, sendto_result);
    EXPECT_EQ(-EOVERFLOW, recv_result);
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    ::close(socket_fds[0]);
    ::close(socket_fds[1]);
}

// Verifies that direct kernel wrappers reject oversize arguments before touching the ring.
TEST_F(io_entity_fixture, kernelic_rejects_oversize_lengths_without_submission) {
    test_kernel_observer observer;
    char byte = 0;
    iovec vec {&byte, 1};
    sockaddr_in address {};
    address.sin_family = AF_INET;
    const auto oversize = static_cast<std::size_t>(UINT_MAX) + 1U;

    // These calls deliberately use no initialized controller: returning false
    // before submit proves the guard precedes every liburing conversion/access.
    EXPECT_FALSE(ace::services::kernel_controller::read(&observer, 0, &byte, oversize, 0));
    EXPECT_FALSE(ace::services::kernel_controller::write(&observer, 0, &byte, oversize, 0));
    EXPECT_FALSE(ace::services::kernel_controller::send(&observer, 0, &byte, oversize, 0));
    EXPECT_FALSE(ace::services::kernel_controller::send_zc(&observer, 0, &byte, oversize, 0, 0));
    EXPECT_FALSE(ace::services::kernel_controller::send_zc_fixed(&observer, 0, &byte, oversize, 0, 0, 0));
    EXPECT_FALSE(ace::services::kernel_controller::sendto(
        &observer,
        0,
        &byte,
        oversize,
        0,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address)));
    EXPECT_FALSE(ace::services::kernel_controller::recv(&observer, 0, &byte, oversize, 0));
    EXPECT_FALSE(ace::services::kernel_controller::writev(&observer, 0, &vec, oversize, 0, 0));
}

// Verifies that a failed io_uring initialization is observable and never touches a null ring.
TEST_F(io_entity_fixture, kernelic_init_failure_reports_availability_and_rejects_ring_operations) {
    const kernel_init_override init_override {reject_kernel_queue_init};
    test_kernel_observer observer;
    int fds[1] = {-1};

    // The injected -EPERM must flow through every direct controller API so no
    // caller can mistake an unusable thread-local ring for a usable one.
    EXPECT_FALSE(ace::services::kernel_controller::available());
    EXPECT_EQ(-EPERM, ace::services::kernel_controller::initialization_error());
    EXPECT_FALSE(ace::services::kernel_controller::nop(&observer));
    EXPECT_EQ(-EPERM, ace::services::kernel_controller::register_files(fds, 1));
    EXPECT_EQ(-EPERM, ace::services::kernel_controller::register_files_update(0, -1));
    EXPECT_EQ(-EPERM, ace::services::kernel_controller::unregister_files());
    EXPECT_FALSE(ace::services::kernel_controller::ping());
    // Status and rejected direct operations must not enqueue a polling service
    // that cannot ever receive a CQE.
    EXPECT_TRUE(ace::empty());
}

// Verifies that an I/O query returns the exact ring initialization error without suspending.
TEST_F(io_entity_fixture, io_query_returns_kernel_init_error_without_submission) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    int result = INT_MIN;
    const kernel_init_override init_override {reject_kernel_queue_init};

    ace::schedule(read_after_kernel_init_failure(pipe_fds[0], result));
    run_dispatcher();

    // The coroutine finishes immediately instead of installing a router that
    // cannot receive a CQE from an unavailable ring.
    EXPECT_EQ(-EPERM, result);
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

// Verifies file output reports init failure without writing through the blocking fallback.
TEST_F(io_entity_fixture, file_output_reports_kernel_init_error_without_fallback_bytes) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    ASSERT_NE(-1, ::fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK));
    ace::fs::file_link output {pipe_fds[1], true};
    outcast_init_failure.store(INT_MIN, std::memory_order_relaxed);
    const outcast_handler_override handler_override {record_outcast_init_failure};
    const kernel_init_override init_override {reject_kernel_queue_init};

    ace::schedule(write_file_link(output, "B38 injected init failure"));
    run_dispatcher();

    // Exact error plus an empty nonblocking pipe proves the failed async submit
    // was observed without silently switching to the blocking writev path.
    EXPECT_EQ(-EPERM, outcast_init_failure.load(std::memory_order_relaxed));
    char byte = 0;
    errno = 0;
    EXPECT_EQ(-1, ::read(pipe_fds[0], &byte, 1));
    EXPECT_EQ(EAGAIN, errno);
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

// Verifies repeated successful outcast completions preserve exact file output without sanitizer leaks.
TEST_F(io_entity_fixture, file_output_reuses_completed_outcast_commands_without_payload_leaks) {
    if (not ace::services::kernel_controller::available()) {
        // Environments that deny io_uring exercise the deterministic failure
        // branch above; successful recycle coverage requires actual CQEs.
        EXPECT_LT(ace::services::kernel_controller::initialization_error(), 0);
        return;
    }

    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    ace::fs::file_link output {pipe_fds[1], true};
    constexpr std::string_view payload {"completed payload"};
    constexpr int repeats = 3;

    ace::schedule(write_file_link(output, payload, repeats));
    run_dispatcher();

    std::array<char, payload.size() * repeats> bytes {};
    // Exact bytes verify all recycled commands reached successful CQEs; the
    // ASan+LSan run supplies the payload-lifetime regression assertion.
    ASSERT_EQ(static_cast<ssize_t>(bytes.size()), ::read(pipe_fds[0], bytes.data(), bytes.size()));
    EXPECT_EQ("completed payloadcompleted payloadcompleted payload",
              std::string_view(bytes.data(), bytes.size()));
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

// Verifies that a stalled connection_link read no longer blocks a sibling timer and is cancelable.
TEST_F(io_entity_fixture, connection_link_stalled_read_keeps_runner_responsive_and_cancels) {
    int socket_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds));
    bool entered = false;
    bool timer_fired = false;
    bool read_completed = false;
    bool join_failed = false;

    {
        ace::net::connection_link link(socket_fds[0], false);
        ace::schedule(cancel_stalled_link_read(
            link,
            entered,
            timer_fired,
            read_completed,
            join_failed));
        run_dispatcher();

        // The timeout is serviced while the receive is pending; the old
        // blocking ::recv path could not reach this line until the peer wrote.
        EXPECT_TRUE(entered);
        EXPECT_TRUE(timer_fired);
        EXPECT_FALSE(read_completed);
        EXPECT_TRUE(join_failed);
    }
    run_dispatcher();
    ::close(socket_fds[1]);
}

// Verifies that connection_link preserves partial data, EOF, and kernel errno results.
TEST_F(io_entity_fixture, connection_link_read_preserves_partial_eof_and_error_results) {
    const std::array<char, 2> payload = {'A', 'B'};
    char data[4] = {};
    int result = INT_MIN;

    int partial_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, partial_fds));
    {
        ace::net::connection_link link(partial_fds[0], false);
        ASSERT_EQ(static_cast<ssize_t>(payload.size()), ::write(partial_fds[1], payload.data(), payload.size()));
        ace::schedule(read_link(link, data, sizeof(data), result));
        run_dispatcher();
        // A short successful result distinguishes partial data from EOF and
        // proves the link uses the receive query's raw byte contract.
        EXPECT_EQ(static_cast<int>(payload.size()), result);
        EXPECT_EQ(payload[0], data[0]);
        EXPECT_EQ(payload[1], data[1]);

        ::close(partial_fds[1]);
        result = INT_MIN;
        ace::schedule(read_link(link, data, sizeof(data), result));
        run_dispatcher();
        EXPECT_EQ(0, result);
    }
    run_dispatcher();

    int error_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, error_fds));
    {
        ace::net::connection_link link(error_fds[0], false);
        // Keeping the object's positive FD while closing the OS descriptor
        // makes io_uring report EBADF instead of triggering query's invalid-FD guard.
        ASSERT_EQ(0, ::close(error_fds[0]));
        result = INT_MIN;
        ace::schedule(read_link(link, data, sizeof(data), result));
        run_dispatcher();
        EXPECT_EQ(-EBADF, result);
    }
    run_dispatcher();
    ::close(error_fds[1]);
}

// Verifies that a connection_link receive resumes on the runner selected before submission.
TEST_F(io_entity_fixture, connection_link_read_preserves_runner_after_migration) {
    ace::cfg::g_config._runners_amount = 2;
    ace::reload();
    ace::bus<ace::core::runner*> runners_channel;
    ace::schedule(gather_runner(runners_channel));
    ace::schedule(gather_runner(runners_channel));
    ace::run();
    const auto runners = fetch(runners_channel);
    ASSERT_EQ(2u, runners.size());
    ASSERT_NE(runners[0], runners[1]);

    int socket_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds));
    ASSERT_EQ(1, ::write(socket_fds[1], "x", 1));
    ace::bus<int> result;
    {
        ace::net::connection_link link(socket_fds[0], false);
        ace::schedule(read_link_after_migration(link, runners[0], runners[1], result));
        ace::run();
        EXPECT_TRUE(ace::empty());
    }
    ace::run();
    ::close(socket_fds[1]);

    const auto values = fetch(result);
    ASSERT_EQ(4u, values.size());
    // The final marker proves completion was routed back to the runner that
    // submitted the receive after both explicit migration steps.
    EXPECT_EQ(1, values[0]);
    EXPECT_EQ(1, values[1]);
    EXPECT_EQ(1, values[2]);
    EXPECT_EQ(1, values[3]);
}

// Verifies that move assignment closes the old descriptor and keeps the incoming one.
TEST_F(io_entity_fixture, entity_move_assignment_releases_old_and_keeps_incoming) {
    int old_pipe[2] = {-1, -1};
    int incoming_pipe[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(old_pipe));
    ASSERT_EQ(0, ::pipe(incoming_pipe));
    const int old_fd = old_pipe[0];
    const int incoming_fd = incoming_pipe[0];

    test_io_entity destination(old_fd, false);
    test_io_entity source(incoming_fd, false);
    destination = std::move(source);

    EXPECT_TRUE(source.is_closed());
    EXPECT_FALSE(source);
    EXPECT_FALSE(destination.is_closed());
    expect_fd_open(old_fd);
    expect_fd_open(incoming_fd);

    // Move assignment dispatches cleanup of the destination's former owner;
    // the transferred descriptor must remain independently valid.
    run_dispatcher();
    expect_fd_closed(old_fd);
    expect_fd_open(incoming_fd);

    { auto close = destination.close(); }
    run_dispatcher();
    expect_fd_closed(incoming_fd);
    ::close(old_pipe[1]);
    ::close(incoming_pipe[1]);
}

// Verifies that self-move assignment is a no-op for descriptor ownership.
TEST_F(io_entity_fixture, entity_self_move_preserves_ownership) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    const int owned_fd = pipe_fds[0];

    test_io_entity entity(owned_fd, false);
    auto* self = &entity;
    entity = std::move(*self);
    EXPECT_TRUE(entity);
    EXPECT_FALSE(entity.is_closed());
    expect_fd_open(owned_fd);

    { auto close = entity.close(); }
    run_dispatcher();
    expect_fd_closed(owned_fd);
    ::close(pipe_fds[1]);
}

// Verifies that awaiting an entity-owned close closes its sole descriptor once.
TEST_F(io_entity_fixture, entity_close_awaited_single_ownership) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    const int owned_fd = pipe_fds[0];
    test_io_entity entity(owned_fd, false);
    int result = INT_MIN;

    ace::schedule(await_close(entity, result));
    run_dispatcher();
    EXPECT_EQ(0, result);
    EXPECT_TRUE(entity.is_closed());
    EXPECT_FALSE(entity);
    expect_fd_closed(owned_fd);
    ::close(pipe_fds[1]);
}

// Verifies that discarding an entity-owned close still dispatches exactly one close.
TEST_F(io_entity_fixture, entity_close_discarded_single_ownership) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    const int owned_fd = pipe_fds[0];

    {
        test_io_entity entity(owned_fd, false);
        { auto close = entity.close(); }
        EXPECT_TRUE(entity.is_closed());
        EXPECT_FALSE(entity);
    }
    run_dispatcher();
    expect_fd_closed(owned_fd);

    // Reusing the exact descriptor number turns a delayed duplicate close into
    // an observable failure instead of a harmless EBADF on the old descriptor.
    ASSERT_EQ(owned_fd, ::dup2(pipe_fds[1], owned_fd));
    expect_fd_open(owned_fd);
    ace::run();
    EXPECT_TRUE(ace::empty());
    expect_fd_open(owned_fd);
    ::close(owned_fd);
    ::close(pipe_fds[1]);
}

// Verifies that repeated close() is an awaited no-op after the first ownership transfer.
TEST_F(io_entity_fixture, entity_close_repeated_is_idempotent) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));
    const int owned_fd = pipe_fds[0];
    test_io_entity entity(owned_fd, false);
    int first_result = INT_MIN;
    int second_result = INT_MIN;

    ace::schedule(await_close_twice(entity, first_result, second_result));
    run_dispatcher();
    EXPECT_EQ(0, first_result);
    EXPECT_EQ(0, second_result);
    EXPECT_TRUE(entity.is_closed());
    EXPECT_FALSE(entity);
    expect_fd_closed(owned_fd);
    ::close(pipe_fds[1]);
}

// Verifies that discarding a directly constructed close_query does not take FD ownership.
TEST_F(io_entity_fixture, direct_close_query_discard_remains_non_owning) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));

    { ace::io::close_query query(pipe_fds[0]); }
    expect_fd_open(pipe_fds[0]);
    EXPECT_TRUE(ace::empty());
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

// Verifies that connection::recv(vector) writes only the vector's logical size.
TEST_F(io_entity_fixture, connection_recv_vector_uses_logical_size) {
    int socket_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds));
    ace::net::connection connection(socket_fds[0], false);
    const std::array<char, 6> payload = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_EQ(
        static_cast<ssize_t>(payload.size()),
        ::write(socket_fds[1], payload.data(), payload.size()));

    std::vector<char> buffer;
    buffer.reserve(32);
    buffer.resize(3, '?');
    int result = INT_MIN;
    ace::schedule(receive_vector(connection, buffer, result));
    run_dispatcher();

    // Capacity is intentionally much larger: receiving three bytes proves the
    // public overload uses size(), not writable-but-logically-absent capacity.
    EXPECT_EQ(3, result);
    EXPECT_EQ((std::vector<char>{'a', 'b', 'c'}), buffer);
    { auto close = connection.close(); }
    run_dispatcher();
    ::close(socket_fds[1]);
}

// Verifies that connection::recv(string) writes only the string's logical size.
TEST_F(io_entity_fixture, connection_recv_string_uses_logical_size) {
    int socket_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds));
    ace::net::connection connection(socket_fds[0], false);
    const std::string payload = "uvwxyz";
    ASSERT_EQ(
        static_cast<ssize_t>(payload.size()),
        ::write(socket_fds[1], payload.data(), payload.size()));

    std::string buffer;
    buffer.reserve(32);
    buffer.resize(3, '?');
    int result = INT_MIN;
    ace::schedule(receive_string(connection, buffer, result));
    run_dispatcher();

    // The reserve/resize difference distinguishes logical writable characters
    // from spare capacity without relying on implementation internals.
    EXPECT_EQ(3, result);
    EXPECT_EQ("uvw", buffer);
    { auto close = connection.close(); }
    run_dispatcher();
    ::close(socket_fds[1]);
}
