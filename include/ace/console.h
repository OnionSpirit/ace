#ifndef ACE_CONSOLE_H
#define ACE_CONSOLE_H

#include <list>
#include <ace/core/async.h>
#include <ace/core/io.h>
#include <ace/fs.h>

// NOTE: It is needed to use external fmt lib with older standards which does not support std::format
#ifndef FMT_SRC
#define FMT_SRC std
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
            co_return co_await _output.read_str();
        }

        template <class... Args>
        static void println(FMT_SRC::format_string<Args...>&& fmt, Args&&... args) {
            _output.writeln(std::forward<FMT_SRC::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static void println(const FMT_SRC::string_view&& str) {
            _output.writeln(std::forward<const FMT_SRC::string_view>(str));
        }

        static void println() {
            _output.writeln("");
        }

        template <class... Args>
        static void print(FMT_SRC::format_string<Args...>&& fmt, Args&&... args) {
            _output.write(std::forward<FMT_SRC::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static void print(const FMT_SRC::string_view&& str) {
            _output.write(std::forward<const FMT_SRC::string_view>(str));
        }

    };

    fs::file_link console::_output { stdout->_fileno };

}

#undef std

#endif //ACE_CONSOLE_H
