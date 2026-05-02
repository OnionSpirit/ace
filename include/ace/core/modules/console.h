#ifndef ACE_CONSOLE_H
#define ACE_CONSOLE_H

#include <fstream>
#include <ace/core/context.h>
#include <ace/core/io.h>
#include <ace/futures/get_runner.h>


namespace ace::core::modules {

    class console {

        console() = default;

    protected:

        struct lazy_print_observer : kernel_observer {

            std::vector<uint8_t> _buffer;

            void on_result(const int res) override {
                auto* self = this;
                _observers_pool.sync(self);
            }

            ~lazy_print_observer() override = default;
        };

        static constexpr int buff_len = 256;

        static std::atomic<std::FILE*> _output;

        static thread_local nukes::dynamic::reg_freelist<lazy_print_observer> _observers_pool;

        static auto get_instance() {
            thread_local console instance {};
            return instance;
        }

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

            bool setup_query(kernel_observer* kwp) {
                // lazy_print_observer* observer_ptr;
                // if (not _observers_pool.capture(observer_ptr))
                    return kernel_controller::write(kwp, _fd, _buff.data(), _buff.size(), 0);
                // io_query_t::_is_silent = true;
                // observer_ptr->_runner_identity = this->_runner_identity;
                // observer_ptr->_buffer.assign(_buff.begin(), _buff.end());
                // return kernel_controller::write(observer_ptr, _fd, _buff.data(), _buff.size(), 0);
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

        struct async {

            [[nodiscard]] static promise<std::string> input() {
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

            template <class... Args>
            static auto println(std::format_string<Args...>&& fmt, Args&&... args) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return println_query(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            template <class... Args>
            static auto println(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args) {
                return println_query(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            static auto println(const std::string_view&& str) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return println_query(file, std::forward<const std::string_view>(str));
            }

            static auto println(const std::FILE* file, const std::string_view&& str) {
                return println_query(file, std::forward<const std::string_view>(str));
            }

            static auto println() {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return println_query(file, "");
            }

            static auto println(const std::FILE* file) {
                return println_query(file, "");
            }

            template <class... Args>
            static auto print(std::format_string<Args...>&& fmt, Args&&... args) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return print_query(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            template <class... Args>
            static auto print(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args) {
                return print_query(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            static auto print(const std::string_view&& str) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return print_query(file, std::forward<const std::string_view>(str));
            }

            static auto print(const std::FILE* file, const std::string_view&& str) {
                return print_query(file, std::forward<const std::string_view>(str));
            }
        };

        [[nodiscard]] static std::string input() {
            std::string s;
            std::cin >> s;
            return s;
        }

        template <class... Args>
        static auto println(std::format_string<Args...>&& fmt, Args&&... args) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return println_busy(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        static auto println(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args) {
            return println_busy(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static auto println(const std::string_view&& str) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return println_busy(file, std::forward<const std::string_view>(str));
        }

        static auto println(const std::FILE* file, const std::string_view&& str) {
            return println_busy(file, std::forward<const std::string_view>(str));
        }

        static auto println() {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return println_busy(file, "");
        }

        static auto println(const std::FILE* file) {
            return println_busy(file, "");
        }

        template <class... Args>
        static auto print(std::format_string<Args...>&& fmt, Args&&... args) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return print_busy(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        static auto print(const std::FILE* file, std::format_string<Args...>&& fmt, Args&&... args) {
            return print_busy(file, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static auto print(const std::string_view&& str) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return print_busy(file, std::forward<const std::string_view>(str));
        }

        static auto print(const std::FILE* file, const std::string_view&& str) {
            return print_busy(file, std::forward<const std::string_view>(str));
        }

    };

    std::atomic<std::FILE*> console::_output = stdout;

    thread_local nukes::dynamic::reg_freelist<console::lazy_print_observer> console::_observers_pool {};

}

#undef std

#endif //ACE_CONSOLE_H
