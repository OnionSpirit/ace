#ifndef ACE_CONSOLE_H
#define ACE_CONSOLE_H

#include <list>
#include <ace/core/async.h>
#include <ace/core/io.h>
#include <ace/futures/get_runner.h>

// NOTE: It is needed to use external fmt lib with older standards which does not support std::format
#ifndef __FMT__
#define __FMT__ std
#endif

namespace ace::core::modules {

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

    protected:

        struct lazy_print_observer : kernel_observer {

            std::vector<uint8_t> _buffer{};

            void on_result(const int res) override {
                _observers_pool.raw_sync(this);
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

        template <class... Args>
        static void print_busy(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            std::string buff;
            buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("print failed: ") + strerror(errno));
        }

        template <class... Args>
        static void println_busy(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            std::string buff;
            buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("print failed: ") + strerror(errno));
        }

        static void print_busy(const std::FILE* file, const __FMT__::string_view&& str) {
            std::string buff;
            buff = std::string(std::forward<const __FMT__::string_view>(str));
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("print failed: ") + strerror(errno));
        }

        static void println_busy(const std::FILE* file, const __FMT__::string_view&& str) {
            std::string buff;
            buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("print failed: ") + strerror(errno));
        }

        template <class... Args>
        static void print_lazy(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            lazy_print_observer* observer_ptr;
            if (not _observers_pool.capture(observer_ptr))
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("print failed: ") + strerror(errno));
            observer_ptr->_runner_identity = nullptr;
            observer_ptr->_buffer.assign(buff.begin(), buff.end());
            if (not kernel_controller::write(observer_ptr, file->_fileno,
                observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                throw std::runtime_error("print failed");
        }

        template <class... Args>
        static void println_lazy(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
            lazy_print_observer* observer_ptr;
            if (not _observers_pool.capture(observer_ptr))
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("print failed: ") + strerror(errno));
            observer_ptr->_runner_identity = nullptr;
            observer_ptr->_buffer.assign(buff.begin(), buff.end());
            if (not kernel_controller::write(observer_ptr, file->_fileno,
                observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                throw std::runtime_error("print failed");
        }

        static void print_lazy(const std::FILE* file, const __FMT__::string_view&& str) {
            const std::string buff = std::string(std::forward<const __FMT__::string_view>(str));
            lazy_print_observer* observer_ptr;
            if (not _observers_pool.capture(observer_ptr))
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("print failed: ") + strerror(errno));
            observer_ptr->_runner_identity = nullptr;
            observer_ptr->_buffer.assign(buff.begin(), buff.end());
            if (not kernel_controller::write(observer_ptr, file->_fileno,
                observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                throw std::runtime_error("print failed");
        }

        static void println_lazy(const std::FILE* file, const __FMT__::string_view&& str) {
            const std::string buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
            lazy_print_observer* observer_ptr;
            if (not _observers_pool.capture(observer_ptr))
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("print failed: ") + strerror(errno));
            observer_ptr->_runner_identity = nullptr;
            observer_ptr->_buffer.assign(buff.begin(), buff.end());
            if (not kernel_controller::write(observer_ptr, file->_fileno,
                observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                throw std::runtime_error("print failed");
        }

    public:

        /**
         * Defines same instrument but powered by io_uring without syscalls
         * @warning Implement async behavior that means that the results of requested operations is not permanent
         */
        struct async {

            [[nodiscard]] static promise<std::expected<std::string, int>> input() {

                std::deque<std::array<char, buff_len>> acc {};
                int total = 0;

                auto& buff = acc.emplace_back();
                int bytes_read = co_await read_query(STDIN_FILENO, buff.data(), buff_len);
                if (bytes_read < 0) co_return std::unexpected(-bytes_read);
                total += bytes_read;

                while (bytes_read == buff_len) {
                    buff = acc.emplace_back();
                    bytes_read = co_await read_query(STDIN_FILENO, buff.data(), buff_len);
                    if (bytes_read < 0) co_return std::unexpected(-bytes_read);
                    total += bytes_read;
                }

                std::string res {};
                // NOTE: + null term char slot
                res.reserve(total + 1);
                for (auto& buf : acc) {
                    const int write_bytes { (total > buff_len) ? buff_len : total };
                    res.append(buf.data(), write_bytes);
                    total -= write_bytes;
                }
                co_return res;
            }

            template <class... Args>
            static auto println(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return println_lazy(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            template <class... Args>
            static auto println(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                return println_lazy(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            static auto println(const __FMT__::string_view&& str) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return println_lazy(file, std::forward<const __FMT__::string_view>(str));
            }

            static auto println(const std::FILE* file, const __FMT__::string_view&& str) {
                return println_lazy(file, std::forward<const __FMT__::string_view>(str));
            }

            static auto println() {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return println_lazy(file, "");
            }

            static auto println(const std::FILE* file) {
                return println_lazy(file, "");
            }

            template <class... Args>
            static auto print(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return print_lazy(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            template <class... Args>
            static auto print(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                return print_lazy(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            static auto print(const __FMT__::string_view&& str) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return print_lazy(file, std::forward<const __FMT__::string_view>(str));
            }

            static auto print(const std::FILE* file, const __FMT__::string_view&& str) {
                return print_lazy(file, std::forward<const __FMT__::string_view>(str));
            }
        };

        [[nodiscard]] static std::string input() {
            std::string s;
            std::cin >> s;
            return s;
        }

        template <class... Args>
        static auto println(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return println_busy(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        static auto println(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return println_busy(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static auto println(const __FMT__::string_view&& str) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return println_busy(file, std::forward<const __FMT__::string_view>(str));
        }

        static auto println(const std::FILE* file, const __FMT__::string_view&& str) {
            return println_busy(file, std::forward<const __FMT__::string_view>(str));
        }

        static auto println() {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return println_busy(file, "");
        }

        static auto println(const std::FILE* file) {
            return println_busy(file, "");
        }

        template <class... Args>
        static auto print(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return print_busy(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        static auto print(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return print_busy(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static auto print(const __FMT__::string_view&& str) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return print_busy(file, std::forward<const __FMT__::string_view>(str));
        }

        static auto print(const std::FILE* file, const __FMT__::string_view&& str) {
            return print_busy(file, std::forward<const __FMT__::string_view>(str));
        }

    };

    std::atomic<std::FILE*> console::_output = stdout;

    thread_local nukes::dynamic::reg_freelist<console::lazy_print_observer> console::_observers_pool {};

}

#undef std

#endif //ACE_CONSOLE_H
