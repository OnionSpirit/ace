#ifndef ACE_CONSOLE_H
#define ACE_CONSOLE_H


#include <list>
#include <format>
#include <ace/core/async.h>
#include <ace/core/io.h>
#include <ace/fs.h>

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
