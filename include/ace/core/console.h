#ifndef ACE_CONSOLE_H
#define ACE_CONSOLE_H

#include <ace/core/context.h>
#include <ace/core/io.h>


namespace ace {

    class console {

        console() = default;

    protected:

        static constexpr int buff_len = 256;

        static auto get_instance() {
            static console instance {};
            return instance;
        }

        // struct abandoned_write_query : core::io_query<abandoned_write_query> {
        //
        //     abandoned_write_query() = delete;
        //
        //     explicit abandoned_write_query(const int fd, const void *buf, const unsigned nbytes, const uint64_t offset = 0)
        //         : io_query(fd)
        //         , _fd(fd)
        //         , _buf(buf)
        //         , _nbytes(nbytes)
        //         , _offset(offset) {
        //         // _abandoned = true;
        //     }
        //
        //     bool setup_query(kernel_observer* kwp) const {
        //         return core::modules::kernel_controller::write(kwp, _fd, _buf, _nbytes, _offset);
        //     }
        //
        //     [[nodiscard]] int await_resume() const { return _res; }
        //
        //     const int _fd;
        //     const void *_buf;
        //     const unsigned _nbytes;
        //     const uint64_t _offset;
        // };

        // template <bool new_line = false>
        // struct ACE_AWAIT_NODISCARD print_query : core::io_query<print_query<new_line>> {
        //
        //     IMPORT_IO_QUERY_ENV(print_query);
        //
        //     print_query() = delete;
        //
        //     template <typename ... Args>
        //     explicit print_query(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args)
        //         : io_query_t(file->_fileno) {
        //         if constexpr (new_line)
        //             _output = std::format(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
        //         else
        //             _output = std::format(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        //     }
        //
        //     explicit print_query(const std::FILE* file, const std::string_view&& str)
        //         : io_query_t(file->_fileno) {
        //         if constexpr (new_line)
        //             _output = std::string(std::forward<const std::string_view>(str)) + '\n';
        //         else
        //             _output = std::string(std::forward<const std::string_view>(str));
        //     }
        //
        //     bool setup_query(core::modules::kernel_observer* kwp) const {
        //         return core::modules::kernel_controller::write(kwp, _fd, _output.data(), _output.size(), 0);
        //     }
        //
        //     void await_resume() const {
        //         if (_res < 0)
        //             throw std::runtime_error(std::string("print failed: ") + strerror(-_res));
        //      }
        //
        //     std::string _output;
        // };
        //
        // typedef print_query<true> println_query;

    public:

        [[nodiscard]] static promise<std::string> input() {
            std::stringstream ss;
            char buff[buff_len] = {};
            int bytes_read = co_await core::read_query(STDIN_FILENO, buff, buff_len);
            ss << buff;
            while (bytes_read == buff_len) {
                bzero(buff, buff_len);
                bytes_read = co_await core::read_query(STDIN_FILENO, buff, buff_len);
                ss << buff;
            }
            co_return ss.str();
        }

        // template <class... Args>
        // static auto println(std::format_string<Args...>&& fmt, Args&&... args) {
        //     return println_query(stdout, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        // }
        //
        // template <class... Args>
        // static auto println(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args) {
        //     return println_query(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        // }
        //
        // static auto println(const std::string_view&& str) {
        //     return println_query(stdout, std::forward<const std::string_view>(str));
        // }
        //
        // static auto println(const std::FILE* file, const std::string_view&& str) {
        //     return println_query(file, std::forward<const std::string_view>(str));
        // }
        //
        // static auto println() {
        //     return println_query(stdout, "");
        // }
        //
        // static auto println(const std::FILE* file) {
        //     return println_query(file, "");
        // }
        //
        // template <class... Args>
        // static auto print(std::format_string<Args...>&& fmt, Args&&... args) {
        //     return print_query(stdout, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        // }
        //
        // template <class... Args>
        // static auto print(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args) {
        //     return print_query(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        // }
        //
        // static auto print(const std::string_view&& str) {
        //     return print_query(stdout, std::forward<const std::string_view>(str));
        // }
        //
        // static auto print(const std::FILE* file, const std::string_view&& str) {
        //     return print_query(file, std::forward<const std::string_view>(str));
        // }

        template <class... Args>
        static promise<void> println(std::format_string<Args...> fmt, Args&&... args) {
            const std::string output = std::format(fmt, std::forward<Args>(args)...) + "\n";
            if (const int res = co_await core::write_query(STDOUT_FILENO, output.data(), output.size()); res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        template <class... Args>
        static promise<void> println(std::FILE* file, std::format_string<Args...> fmt, Args&&... args) {
            const std::string output = std::format(fmt, std::forward<Args>(args)...) + "\n";
            if (const int res = co_await core::write_query(file->_fileno, output.data(), output.size()); res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        static promise<void> println(std::string str) {
            str += '\n';
            if (str.empty()) co_return;
            if (const int res = co_await core::write_query(STDOUT_FILENO, str.data(), str.size()); res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
            co_return;
        }

        static promise<void> println(const std::FILE* file, std::string str) {
            str += '\n';
            if (str.empty()) co_return;
            if (const int res = co_await core::write_query(file->_fileno, str.data(), str.size()); res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        static promise<void> println() {
            const std::string output {'\n'};
            if (const int res = co_await core::write_query(STDOUT_FILENO, output.data(), output.size()); res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        static promise<void> println(const std::FILE* file) {
            const std::string output {'\n'};
            if (const int res = co_await core::write_query(file->_fileno, output.data(), output.size()); res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        template <class... Args>
        static promise<void> print(std::format_string<Args...> fmt, Args&&... args) {
            const std::string output = std::format(fmt, std::forward<Args>(args)...);
            if (const int res = co_await core::write_query(STDOUT_FILENO, output.data(), output.size()); res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        template <class... Args>
        static promise<void> print(std::FILE* file, std::format_string<Args...> fmt, Args&&... args) {
            const std::string output = std::format(fmt, std::forward<Args>(args)...);
            if (const int res = co_await core::write_query(file->_fileno, output.data(), output.size()); res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        static promise<void> print(const std::string_view str) {
            if (str.empty()) co_return;
            if (const int res = co_await core::write_query(STDOUT_FILENO, str.data(), str.size()); res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
            co_return;
        }

        static promise<void> print(const std::FILE* file, const std::string_view str) {
            if (str.empty()) co_return;
            if (const int res = co_await core::write_query(file->_fileno, str.data(), str.size()); res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

    };

}

#undef std

#endif //ACE_CONSOLE_H
