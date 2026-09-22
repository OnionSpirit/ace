#include <fcntl.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <unistd.h>

#include "environment.h"

#include <ace/console.h>
#include <ace/fs.h>

namespace {

struct fs_fixture : base_fixture {
    static constexpr const char* rewrite_path = "test_open_rewrite_truncates.txt";
    static constexpr const char* self_move_path = "test_file_self_move.txt";

    static ace::task fs_testing() {
        auto f = ace::fs::file("flexing.txt");
        if (auto f_entity = co_await f.open(O_CREAT | O_WRONLY | O_TRUNC))
            f_entity.writeln("testing flex {}", 1);
    }

    static ace::task file_write_and_read_task() {
        auto f1 = ace::fs::file("test_write_read.txt");
        if (auto f_entity = co_await f1.open(O_CREAT | O_WRONLY | O_TRUNC))
            f_entity.writeln("hello fs");

        auto f2 = ace::fs::file("test_write_read.txt");
        if (auto f_entity = co_await f2.open(O_RDONLY)) {
            auto result = co_await f_entity.read_buf();
            if (result) {
                auto content = result.value().as<std::string>();
                ace::println("read: '{}'", content);
            }
        }
        co_return;
    }

    static ace::task file_open_fail_task(ace::bus<bool>& result) {
        auto f = ace::fs::file("nonexistent_file_12345.txt");
        if (auto f_entity = co_await f.open(O_RDONLY))
            result << false;
        else
            result << true;
        co_return;
    }

    static ace::task open_rewrite_task(const char* path, ace::bus<bool>& result) {
        auto f = ace::fs::file(path);
        auto f_entity = co_await f.open_rewrite();
        result << static_cast<bool>(f_entity);
        co_return;
    }

    static ace::task self_move_open_task(std::filesystem::path path,
                                         bool& io_available, bool& opened) {
        ace::fs::file file(path);
        auto* self = &file;
        file = std::move(*self);
        io_available = ace::services::kernel_controller::available();
        if (not io_available)
            co_return;
        auto link = co_await file.open_rdonly();
        opened = static_cast<bool>(link);
        co_return;
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(rewrite_path, error);
        std::filesystem::remove(self_move_path, error);
    }
};

// Verifies that a scheduled filesystem task opens and writes a new file.
TEST_F(fs_fixture, do_fs_tests) {
    ace::schedule(fs_testing());
    ace::run();
    ASSERT_TRUE(ace::empty());
}

// Verifies the existing asynchronous file write-and-read workflow completes.
TEST_F(fs_fixture, file_write_and_read) {
    ace::schedule(file_write_and_read_task());
    ace::run();
    EXPECT_TRUE(ace::empty());
}

// Verifies opening a nonexistent file read-only produces an invalid file link.
TEST_F(fs_fixture, file_open_fail) {
    ace::bus<bool> result;
    ace::schedule(file_open_fail_task(result));
    ace::run();
    EXPECT_TRUE(ace::empty());
    auto res = fetch(result);
    ASSERT_GE(res.size(), 1u);
    // true is published only by the expected failed-open branch.
    EXPECT_TRUE(res[0]);
}

// Verifies open_rewrite truncates an existing file before returning its link.
TEST_F(fs_fixture, open_rewrite_truncates_existing_file) {
    {
        std::ofstream existing(rewrite_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(existing.is_open());
        existing << "content that must be removed";
    }
    ASSERT_GT(std::filesystem::file_size(rewrite_path), 0u);

    ace::bus<bool> result;
    ace::schedule(open_rewrite_task(rewrite_path, result));
    ace::run();
    ASSERT_TRUE(ace::empty());
    const auto res = fetch(result);
    ASSERT_EQ(res.size(), 1u);
    ASSERT_TRUE(res[0]);
    // File size directly observes O_TRUNC without relying on buffered write timing.
    EXPECT_EQ(std::filesystem::file_size(rewrite_path), 0u);
}

// Verifies that self-move leaves the file path and idle descriptor state unchanged.
TEST_F(fs_fixture, file_self_move_preserves_path) {
    const std::filesystem::path path(self_move_path);
    ace::fs::file file(path);
    auto* self = &file;
    file = std::move(*self);

    // The path is public state; checking it keeps this regression independent of io_uring.
    EXPECT_EQ(path, file._path);
    EXPECT_TRUE(file.is_closed());
}

// Verifies that self-move retains an owned descriptor in the file entity.
TEST_F(fs_fixture, file_self_move_preserves_descriptor_ownership) {
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(pipe_fds));

    ace::fs::file file(self_move_path);
    // open() consumes the entity, so use the public base assignment to exercise
    // self-move while the file still owns a descriptor.
    auto& base = static_cast<ace::io::entity<ace::fs::file>&>(file);
    base = ace::io::entity<ace::fs::file>{pipe_fds[0], false};
    auto* self = &file;
    file = std::move(*self);

    EXPECT_FALSE(file.is_closed());
    auto [fd, closed] = file.extract();
    // Extracting the same live FD proves sole ownership survived without io_uring.
    EXPECT_EQ(pipe_fds[0], fd);
    EXPECT_FALSE(closed);
    EXPECT_NE(-1, ::fcntl(fd, F_GETFD));
    ::close(fd);
    ::close(pipe_fds[1]);
}

// Verifies ordinary move assignment still transfers the source file path.
TEST_F(fs_fixture, file_move_assignment_transfers_path) {
    ace::fs::file destination("previous.txt");
    ace::fs::file source("incoming.txt");
    auto& assigned = destination = std::move(source);

    // The destination must use the incoming pathname after its old state is replaced.
    EXPECT_EQ(std::filesystem::path("incoming.txt"), destination._path);
    EXPECT_EQ(&destination, &assigned);
    EXPECT_TRUE(destination.is_closed());
}

// Verifies that a self-moved file still opens its original path when io_uring is available.
TEST_F(fs_fixture, file_self_move_still_opens_original_path) {
    {
        std::ofstream existing(self_move_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(existing.is_open());
        existing << "self-move target";
    }

    bool io_available = false;
    bool opened = false;
    ace::schedule(self_move_open_task(self_move_path, io_available, opened));
    ace::run();
    ASSERT_TRUE(ace::empty());
    if (not io_available)
        GTEST_SKIP() << "io_uring is unavailable in this environment";
    // Opening the original file checks the public behavior after self-move.
    EXPECT_TRUE(opened);
}

} // namespace
