#ifndef ACE_CONSOLE_H
#define ACE_CONSOLE_H

#include <fstream>
#include <ace/core/context.h>
#include <ace/core/io.h>


namespace ace::core::modules {

    enum console_mode {
        e_blocking,
        e_async
    };

    template <console_mode mode = e_blocking>
    class console {

        console() = default;

    protected:

        static constexpr int buff_len = 256;

        static std::atomic<std::FILE*> _output;

        static auto get_instance() {
            static console instance {};
            return instance;
        }

        // TODO: Make it abandoned on iovec kernelic support
        template <bool new_line = false>
        struct ACE_AWAIT_NODISCARD print_query : io_query<print_query<new_line>> {

            IMPORT_IO_QUERY_ENV(print_query);

            print_query() = delete;

            template <typename ... Args>
            explicit print_query(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args)
                : io_query_t(file->_fileno) {
                if constexpr (new_line)
                    _buff = std::format(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
                else
                    _buff = std::format(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            explicit print_query(const std::FILE* file, const std::string_view&& str)
                : io_query_t(file->_fileno) {
                if constexpr (new_line)
                    _buff = std::string(std::forward<const std::string_view>(str)) + '\n';
                else
                    _buff = std::string(std::forward<const std::string_view>(str));
            }

            bool setup_query(kernel_observer* kwp) const {
                return kernel_controller::write(kwp, _fd, _buff.data(), _buff.size(), 0);
            }

            void await_resume() const {
                if (_res < 0)
                    throw std::runtime_error(std::string("print failed: ") + strerror(-_res));
             }

            std::string _buff;
        };

        template <class... Args>
        static void print_busy(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args) {
            std::string buff;
            buff = std::format(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("print failed: ") + strerror(errno));
        }

        template <class... Args>
        static void println_busy(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args) {
            std::string buff;
            buff = std::format(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("print failed: ") + strerror(errno));
        }

        static void print_busy(const std::FILE* file, const std::string_view&& str) {
            std::string buff;
            buff = std::string(std::forward<const std::string_view>(str));
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("print failed: ") + strerror(errno));
        }

        static void println_busy(const std::FILE* file, const std::string_view&& str) {
            std::string buff;
            buff = std::string(std::forward<const std::string_view>(str)) + '\n';
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("print failed: ") + strerror(errno));
        }

        typedef print_query<true> println_query;

    public:

        [[nodiscard]] static promise<std::string> input() requires (mode == e_async) {
            std::stringstream ss;
            char buff[buff_len] = {};
            int bytes_read = co_await read_query(STDIN_FILENO, buff, buff_len);
            ss << buff;
            while (bytes_read == buff_len) {
                bzero(buff, buff_len);
                bytes_read = co_await read_query(STDIN_FILENO, buff, buff_len);
                ss << buff;
            }
            co_return ss.str();
        }

        [[nodiscard]] static std::string input() requires (mode == e_blocking) {
            std::string s;
            std::cin >> s;
            return s;
        }

        template <class... Args>
        static auto println(std::format_string<Args...>&& fmt, Args&&... args) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            if constexpr (mode == e_async)
                return println_query(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            else
                return println_busy(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        static auto println(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args) {
            if constexpr (mode == e_async)
                return println_query(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            else
                return println_busy(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static auto println(const std::string_view&& str) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            if constexpr (mode == e_async)
                return println_query(file, std::forward<const std::string_view>(str));
            else
                return println_busy(file, std::forward<const std::string_view>(str));
        }

        static auto println(const std::FILE* file, const std::string_view&& str) {
            if constexpr (mode == e_async)
                return println_query(file, std::forward<const std::string_view>(str));
            else
                return println_busy(file, std::forward<const std::string_view>(str));
        }

        static auto println() {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            if constexpr (mode == e_async)
                return println_query(file, "");
            else
                return println_busy(file, "");
        }

        static auto println(const std::FILE* file) {
            if constexpr (mode == e_async)
                return println_query(file, "");
            else
                return println_busy(file, "");
        }

        template <class... Args>
        static auto print(std::format_string<Args...>&& fmt, Args&&... args) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            if constexpr (mode == e_async)
                return print_query(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            else
                return print_busy(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        static auto print(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args) {
            if constexpr (mode == e_async)
                return print_query(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            else
                return print_busy(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static auto print(const std::string_view&& str) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            if constexpr (mode == e_async)
                return print_query(file, std::forward<const std::string_view>(str));
            else
                return print_busy(file, std::forward<const std::string_view>(str));
        }

        static auto print(const std::FILE* file, const std::string_view&& str) {
            if constexpr (mode == e_async)
                return print_query(file, std::forward<const std::string_view>(str));
            else
                return print_busy(file, std::forward<const std::string_view>(str));
        }

    };

    template<console_mode mode>
    std::atomic<std::FILE*> console<mode>::_output = stdout;

}

#undef std

#endif //ACE_CONSOLE_H
