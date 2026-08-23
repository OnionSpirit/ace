#include <fcntl.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "environment.h"

#include <ace/console.h>
#include <ace/fs.h>

namespace {

struct fs_fixture : base_fixture {
    static constexpr const char* rewrite_path = "test_open_rewrite_truncates.txt";

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

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(rewrite_path, error);
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

} // namespace
