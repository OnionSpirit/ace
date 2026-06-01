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

        struct stdio_link : core::io_link {

            IMPORT_IO_LINK_ENV(stdio_link);
            IMPORT_IO_LINK_FABRICATION;

            stdio_link() { _fd = -1; _is_closed = true; }

            void output_action(const std::span<const char> buff) override {
                // NOTE: Trying to get thread local runner from the dispatcher
                auto* runner_identity = reinterpret_cast<runner_pool_t*>(core::dispatcher::get_local_runner());
                // NOTE: If can not get slot or identity not found -> using busy behavior
                if (core::io_hanged::command* cmd; not core::io_hanged::_command_pool.capture(cmd) or not runner_identity) [[unlikely]] {
                    if (::write(stdout->_fileno, buff.data(), buff.size()) < 0 and core::io_hanged::fail_cb_handler)
                        core::io_hanged::fail_cb_handler(errno);
                }
                // NOTE: Pushing data to slot, and setting identity for kernelic
                else [[likely]] {
                    cmd->_runner_identity = runner_identity;
                    cmd->_buffer.assign(buff.begin(), buff.end());
                    if (not core::services::kernel_controller::write(cmd, stdout->_fileno,
                        cmd->_buffer.data(), cmd->_buffer.size(), 0) and core::io_hanged::fail_cb_handler)
                        core::io_hanged::fail_cb_handler(EAGAIN); // Maybe EIO?
                }
            };

            async<int> input_action(void *buff, const std::size_t len) override {
                co_return co_await core::read_query(stdin->_fileno, buff, len);
            }

        };

        // TODO: Make io_router and upgrade this field to it
        static stdio_link _stdio;

    public:

        [[nodiscard]] static async<std::expected<std::string, int>> input() {
            co_return co_await _stdio.read_str();
        }

        template <class... Args>
        static void println(FMT_SRC::format_string<Args...>&& fmt, Args&&... args) {
            _stdio.writeln(std::forward<FMT_SRC::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static void println(const FMT_SRC::string_view&& str) {
            _stdio.writeln(std::forward<const FMT_SRC::string_view>(str));
        }

        static void println() {
            _stdio.writeln("");
        }

        template <class... Args>
        static void print(FMT_SRC::format_string<Args...>&& fmt, Args&&... args) {
            _stdio.write(std::forward<FMT_SRC::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static void print(const FMT_SRC::string_view&& str) {
            _stdio.write(std::forward<const FMT_SRC::string_view>(str));
        }

    };

    console::stdio_link console::_stdio {};

}

#undef std

#endif //ACE_CONSOLE_H
