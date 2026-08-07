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

        /// @brief Private constructor — @c console is a static utility class.
        console() = default;

        /// @brief Shared file link for stdin.
        static io::slink _input;
        /// @brief Shared file link for stdout.
        static io::slink _output;

    public:

        /**
         * @brief Asynchronously read a line (or data chunk) from stdin.
         * @return The read input data as @c io::input_t.
         */
        [[nodiscard]] static async<io::input_t> input() {
            co_return co_await _input->read_buf();
        }

        /**
         * @brief Format and write a string to stdout followed by a newline.
         * @tparam Args Format argument types.
         * @param fmt  Format string.
         * @param args Format arguments.
         */
        template <class... Args>
        static void println(std::format_string<Args...>&& fmt, Args&&... args) {
            _output->writeln(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        /**
         * @brief Write a string to stdout followed by a newline.
         * @param str String to write.
         */
        static void println(const std::string_view&& str) {
            _output->writeln(std::forward<const std::string_view>(str));
        }

        /// @brief Write an empty line to stdout.
        static void println() {
            _output->writeln("");
        }

        /**
         * @brief Write a scatter-gather buffer to stdout followed by a newline.
         * @param buf Buffer to write.
         */
        static void println(io::buffer&& buf) {
            _output->writeln(std::forward<io::buffer>(buf));
        }

        /**
         * @brief Format and write a string to stdout without a newline.
         * @tparam Args Format argument types.
         * @param fmt  Format string.
         * @param args Format arguments.
         */
        template <class... Args>
        static void print(std::format_string<Args...>&& fmt, Args&&... args) {
            _output->write(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        /**
         * @brief Write a string to stdout without a newline.
         * @param str String to write.
         */
        static void print(const std::string_view&& str) {
            _output->write(std::forward<const std::string_view>(str));
        }

        /**
         * @brief Write a scatter-gather buffer to stdout without a newline.
         * @param buf Buffer to write.
         */
        static void print(io::buffer&& buf) {
            _output->write(std::forward<io::buffer>(buf));
        }

        /**
         * @brief I/O file link for stdin. Marked closed to not actually close this descriptor by RAII.
         * @return Shared pointer to the stdin link.
         */
        static auto stdin_link() -> std::shared_ptr<fs::file_link> {
            static auto in = std::make_shared<fs::file_link>(stdin->_fileno , true);
            return in;
        }

        /**
         * @brief I/O file link for stdout. Marked closed to not actually close this descriptor by RAII.
         * @return Shared pointer to the stdout link.
         */
        static auto stdout_link() -> std::shared_ptr<fs::file_link> {
            static auto out = std::make_shared<fs::file_link>(stdout->_fileno , true);
            return out;
        }

        /**
         * @brief Get (or replace) the shared stdin link.
         * @param link New stdin link to install; defaults to the current one.
         * @return The installed stdin link.
         */
        static auto input_link(io::slink link = _input) {
            return _input = std::move(link);
        };

        /**
         * @brief Get (or replace) the shared stdout link.
         * @param link New stdout link to install; defaults to the current one.
         * @return The installed stdout link.
         */
        static auto output_link(io::slink link = _output) {
            return _output = std::move(link);
        };

    };

    inline io::slink console::_input = stdin_link();
    inline io::slink console::_output = stdout_link();

}

#undef std

#endif //ACE_CONSOLE_H
