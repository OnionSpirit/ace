#ifndef ACE_CONSOLE_H
#define ACE_CONSOLE_H

#include <list>
#include <ace/core/async.h>
#include <ace/core/io.h>
#include <ace/fs.h>

// NOTE: It is needed to use external fmt lib with older standards which does not support std::format
#ifndef __FMT__
#define __FMT__ std
#endif

namespace ace {

    /**
     * @warning Don't take it serious. For the most part this is a dumb joke.
     * 0, 1, 2, 3, 4... Guinnesses and then things went wrong.
     * Actually I've just wanned an abstraction for the async input.
     * I've improved this thing up just to debug @c vortex mechanism and @c kernelic module.
     * @c console module helped me to reveal a bunch of shitty bugs.
     * That is why console has a set of async print functions.
     * There are no blazing features just printing via @c io_uring
     */
    class console {

        console() = default;

        // TODO: Make io_router and upgrade this field to it
        static fs::file_link _output;

    public:

        [[nodiscard]] static promise<std::expected<std::string, int>> input() {

            std::deque<std::array<char, core::io_link::buff_len>> acc {};
            int total = 0;

            auto& buff = acc.emplace_back();
            int bytes_read = co_await core::read_query(STDIN_FILENO, buff.data(), core::io_link::buff_len);
            if (bytes_read < 0) co_return std::unexpected(-bytes_read);
            total += bytes_read;

            while (bytes_read == core::io_link::buff_len) {
                buff = acc.emplace_back();
                bytes_read = co_await core::read_query(STDIN_FILENO, buff.data(), core::io_link::buff_len);
                if (bytes_read < 0) co_return std::unexpected(-bytes_read);
                total += bytes_read;
            }

            std::string res {};
            // NOTE: + null term char slot
            res.reserve(total + 1);
            for (auto& buf : acc) {
                const int write_bytes { (total > core::io_link::buff_len) ? core::io_link::buff_len : total };
                res.append(buf.data(), write_bytes);
                total -= write_bytes;
            }
            co_return res;
        }

        template <class... Args>
        static void println(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            _output.writeln(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static void println(const __FMT__::string_view&& str) {
            _output.writeln(std::forward<const __FMT__::string_view>(str));
        }

        static void println() {
            _output.writeln("");
        }

        template <class... Args>
        static void print(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            _output.write(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static void print(const __FMT__::string_view&& str) {
            _output.write(std::forward<const __FMT__::string_view>(str));
        }

    };

    fs::file_link console::_output { stdout->_fileno };

}

#undef std

#endif //ACE_CONSOLE_H
