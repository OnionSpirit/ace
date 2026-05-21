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
        static void print_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
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
        static void println_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
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

        static void print_impl(const std::FILE* file, const __FMT__::string_view&& str) {
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

        static void println_impl(const std::FILE* file, const __FMT__::string_view&& str) {
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
            return println_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        static auto println(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return println_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static auto println(const __FMT__::string_view&& str) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return println_impl(file, std::forward<const __FMT__::string_view>(str));
        }

        static auto println(const std::FILE* file, const __FMT__::string_view&& str) {
            return println_impl(file, std::forward<const __FMT__::string_view>(str));
        }

        static auto println() {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return println_impl(file, "");
        }

        static auto println(const std::FILE* file) {
            return println_impl(file, "");
        }

        template <class... Args>
        static auto print(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return print_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        static auto print(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return print_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static auto print(const __FMT__::string_view&& str) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return print_impl(file, std::forward<const __FMT__::string_view>(str));
        }

        static auto print(const std::FILE* file, const __FMT__::string_view&& str) {
            return print_impl(file, std::forward<const __FMT__::string_view>(str));
        }

        /**
         * The operations are powered by standard io functions
         */
        class busy {

            busy() = default;

            template <class... Args>
            static void print_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                std::string buff;
                buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("print failed: ") + strerror(errno));
            }

            template <class... Args>
            static void println_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                std::string buff;
                buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("print failed: ") + strerror(errno));
            }

            static void print_impl(const std::FILE* file, const __FMT__::string_view&& str) {
                std::string buff;
                buff = std::string(std::forward<const __FMT__::string_view>(str));
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("print failed: ") + strerror(errno));
            }

            static void println_impl(const std::FILE* file, const __FMT__::string_view&& str) {
                std::string buff;
                buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("print failed: ") + strerror(errno));
            }

        public:

            [[nodiscard]] static std::string input() {
                std::string s;
                std::cin >> s;
                return s;
            }

            template <class... Args>
            static auto println(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return busy::println_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            template <class... Args>
            static auto println(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                return busy::println_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            static auto println(const __FMT__::string_view&& str) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return busy::println_impl(file, std::forward<const __FMT__::string_view>(str));
            }

            static auto println(const std::FILE* file, const __FMT__::string_view&& str) {
                return busy::println_impl(file, std::forward<const __FMT__::string_view>(str));
            }

            static auto println() {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return busy::println_impl(file, "");
            }

            static auto println(const std::FILE* file) {
                return busy::println_impl(file, "");
            }

            template <class... Args>
            static auto print(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return busy::print_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            template <class... Args>
            static auto print(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                return busy::print_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            static auto print(const __FMT__::string_view&& str) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return busy::print_impl(file, std::forward<const __FMT__::string_view>(str));
            }

            static auto print(const std::FILE* file, const __FMT__::string_view&& str) {
                return busy::print_impl(file, std::forward<const __FMT__::string_view>(str));
            }
        };

        /**
         * @brief The operations are attached to a current worker(runner) for processing. Useless for single thread app
         * @warning @c co_await operator required
         */
        class attach {

            attach() = default;

            template <bool new_line = false>
            struct ACE_AWAIT_NODISCARD print_impl : io_query<print_impl<new_line>> {

                IMPORT_IO_QUERY_ENV(print_impl);

                print_impl() = delete;

                template <typename ... Args>
                explicit print_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args)
                    : io_query_t(file->_fileno) {
                    if constexpr (new_line)
                        _buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
                    else
                        _buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
                }

                explicit print_impl(const std::FILE* file, const __FMT__::string_view&& str)
                    : io_query_t(file->_fileno) {
                    if constexpr (new_line)
                        _buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
                    else
                        _buff = std::string(std::forward<const __FMT__::string_view>(str));
                }

                bool setup_query(kernel_observer* kwp) {
                    lazy_print_observer* observer_ptr;
                    if (not _observers_pool.capture(observer_ptr))
                        return kernel_controller::write(kwp, _fd, _buff.data(), _buff.size(), 0);
                    io_query_t::_is_silent = true;
                    observer_ptr->_runner_identity = this->_runner_identity;
                    observer_ptr->_buffer.assign(_buff.begin(), _buff.end());
                    return kernel_controller::write(observer_ptr, _fd,
                        observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0);
                }

                void await_resume() const { }

                std::string _buff;
            };

            typedef print_impl<true> println_impl;

        public:

            template <class... Args>
            static auto println(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return attach::println_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            template <class... Args>
            static auto println(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                return attach::println_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            static auto println(const __FMT__::string_view&& str) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return attach::println_impl(file, std::forward<const __FMT__::string_view>(str));
            }

            static auto println(const std::FILE* file, const __FMT__::string_view&& str) {
                return attach::println_impl(file, std::forward<const __FMT__::string_view>(str));
            }

            static auto println() {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return attach::println_impl(file, "");
            }

            static auto println(const std::FILE* file) {
                return attach::println_impl(file, "");
            }

            template <class... Args>
            static auto print(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return attach::print_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            template <class... Args>
            static auto print(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                return attach::print_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            static auto print(const __FMT__::string_view&& str) {
                const std::FILE* file = _output.load(std::memory_order_acquire);
                return attach::print_impl(file, std::forward<const __FMT__::string_view>(str));
            }

            static auto print(const std::FILE* file, const __FMT__::string_view&& str) {
                return attach::print_impl(file, std::forward<const __FMT__::string_view>(str));
            }
        };

    };

    std::atomic<std::FILE*> console::_output = stdout;

    thread_local nukes::dynamic::reg_freelist<console::lazy_print_observer> console::_observers_pool {};

}

#undef std

#endif //ACE_CONSOLE_H
