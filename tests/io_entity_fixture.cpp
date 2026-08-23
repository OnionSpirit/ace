#include <array>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <ace/ace.h>
#include <ace/io.h>
#include <ace/net.h>

struct test_io_entity : ace::io::entity<test_io_entity> {
    IMPORT_IO_ENTITY_ENV(test_io_entity)
    IMPORT_IO_ENTITY_FABRICATION
};

struct io_entity_fixture : ::testing::Test {
    struct exact_read_storage {
        std::array<unsigned char, 4> bytes {0xCC, 0xCC, 0xCC, 0xCC};
        unsigned char canary = 0xA5;
    };

    static ace::task read_exact(const int fd, exact_read_storage& storage, int& result) {
        result = co_await ace::io::read_query(fd, storage.bytes.data(), storage.bytes.size());
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
