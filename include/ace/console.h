/**
 * @file console.h
 * @brief Async console I/O — stdin/stdout wrappers using @c io_uring.
 *
 * @details The @c ace::console class provides async @c input() (reads from
 * stdin via @c file_link::read_str()) and sync @c print()/@c println()
 * (writes to stdout via @c file_link::writeln()/write()).  Both stdin and
 * stdout are represented as @c ace::fs::file_link instances marked as
 * "already closed" to prevent RAII from closing the actual stdio descriptors.
 *
 * @see ace::fs::file_link
 */
#ifndef ACE_CONSOLE_H
#define ACE_CONSOLE_H


#include <list>
#include <format>
#include <ace/core/async.h>
#include <ace/core/io.h>
#include <ace/fs.h>

namespace ace {

    /**
     * @brief Async console I/O — prints to stdout, reads from stdin.
     *
     * @details Uses @c ace::fs::file_link internally for both streams.
     * @c input() is an async coroutine; @c print()/@c println() are
     * synchronous (they delegate to @c file_link methods which internally
     * dispatch via @c io_uring or blocking fallback).
     */
    class console {

        console() = default;

        // TODO: Make io_router and upgrade this fields to it
        static fs::file_link _stdin;
        static fs::file_link _stdout;

    public:

        [[nodiscard]] static async<std::expected<std::string, int>> input() {
            co_return co_await _stdin.read_str();
        }

        template <class... Args>
        static void println(std::format_string<Args...>&& fmt, Args&&... args) {
            _stdout.writeln(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static void println(const std::string_view&& str) {
            _stdout.writeln(std::forward<const std::string_view>(str));
        }

        static void println() {
            _stdout.writeln("");
        }

        template <class... Args>
        static void print(std::format_string<Args...>&& fmt, Args&&... args) {
            _stdout.write(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static void print(const std::string_view&& str) {
            _stdout.write(std::forward<const std::string_view>(str));
        }

    };

    // NOTE: I/O File links for stdio. Marked closed to not actually close this descriptors by RAII
    inline fs::file_link console::_stdin  { stdin->_fileno , true };
    inline fs::file_link console::_stdout { stdout->_fileno, true };

}

#undef std

#endif //ACE_CONSOLE_H
