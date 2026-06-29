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
#include <utility>
#include <ace/core/async.h>
#include <ace/io.h>
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

        static io::reactive_link _input;
        static io::reactive_link _output;

    public:

        [[nodiscard]] static async<std::expected<io::buffer, int>> input() {
            co_return co_await _input->read_buf();
        }

        template <class... Args>
        static void println(std::format_string<Args...>&& fmt, Args&&... args) {
            _output->writeln(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static void println(const std::string_view&& str) {
            _output->writeln(std::forward<const std::string_view>(str));
        }

        static void println() {
            _output->writeln("");
        }

        static void println(const io::buffer&& buf) {
            _output->writeln(std::forward<const io::buffer>(buf));
        }

        template <class... Args>
        static void print(std::format_string<Args...>&& fmt, Args&&... args) {
            _output->write(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static void print(const std::string_view&& str) {
            _output->write(std::forward<const std::string_view>(str));
        }

        static void print(const io::buffer&& buf) {
            _output->write(std::forward<const io::buffer>(buf));
        }

        // NOTE: I/O File links for stdio. Marked closed to not actually close this descriptors by RAII
        static auto stdin_link() -> std::shared_ptr<fs::file_link> {
            static auto in = std::make_shared<fs::file_link>(stdin->_fileno , true);
            return in;
        }

        static auto stdout_link() -> std::shared_ptr<fs::file_link> {
            static auto out = std::make_shared<fs::file_link>(stdout->_fileno , true);
            return out;
        }

        static auto input_link(io::reactive_link link = _input) {
            return _input = std::move(link);
        };

        static auto output_link(io::reactive_link link = _output) {
            return _output = std::move(link);
        };

    };

    inline io::reactive_link console::_input = stdin_link();
    inline io::reactive_link console::_output = stdout_link();

}

#undef std

#endif //ACE_CONSOLE_H
