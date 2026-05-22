#ifndef ACE_FS_H
#define ACE_FS_H

#include <list>
#include <ace/core/async.h>
#include <ace/core/io.h>
#include <ace/futures/get_runner.h>

// NOTE: It is needed to use external fmt lib with older standards which does not support std::format
#ifndef __FMT__
#define __FMT__ std
#endif

namespace ace::core::futures {

    class file {

        struct lazy_write_observer : modules::kernel_observer {

            std::vector<uint8_t> _buffer{};

            void on_result(const int res) override {
                _observers_pool.raw_sync(this);
            }

            ~lazy_write_observer() override = default;
        };

        static constexpr int buff_len = 256;

        static std::atomic<std::FILE*> _output;

        static thread_local nukes::dynamic::reg_freelist<lazy_write_observer> _observers_pool;

        static auto get_instance() {
            thread_local file instance {};
            return instance;
        }

        template <class... Args>
        static void write_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            lazy_write_observer* observer_ptr;
            if (not _observers_pool.capture(observer_ptr))
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("write failed: ") + strerror(errno));
            observer_ptr->_runner_identity = nullptr;
            observer_ptr->_buffer.assign(buff.begin(), buff.end());
            if (not modules::kernel_controller::write(observer_ptr, file->_fileno,
                observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                throw std::runtime_error("write failed");
        }

        template <class... Args>
        static void writeln_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
            lazy_write_observer* observer_ptr;
            if (not _observers_pool.capture(observer_ptr))
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("write failed: ") + strerror(errno));
            observer_ptr->_runner_identity = nullptr;
            observer_ptr->_buffer.assign(buff.begin(), buff.end());
            if (not modules::kernel_controller::write(observer_ptr, file->_fileno,
                observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                throw std::runtime_error("write failed");
        }

        static void write_impl(const std::FILE* file, const __FMT__::string_view&& str) {
            const std::string buff = std::string(std::forward<const __FMT__::string_view>(str));
            lazy_write_observer* observer_ptr;
            if (not _observers_pool.capture(observer_ptr))
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("write failed: ") + strerror(errno));
            observer_ptr->_runner_identity = nullptr;
            observer_ptr->_buffer.assign(buff.begin(), buff.end());
            if (not modules::kernel_controller::write(observer_ptr, file->_fileno,
                observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                throw std::runtime_error("write failed");
        }

        static void writeln_impl(const std::FILE* file, const __FMT__::string_view&& str) {
            const std::string buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
            lazy_write_observer* observer_ptr;
            if (not _observers_pool.capture(observer_ptr))
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("write failed: ") + strerror(errno));
            observer_ptr->_runner_identity = nullptr;
            observer_ptr->_buffer.assign(buff.begin(), buff.end());
            if (not modules::kernel_controller::write(observer_ptr, file->_fileno,
                observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                throw std::runtime_error("write failed");
        }

    public:

        file() = default;

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
        auto writeln(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return writeln_impl(_desc, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        auto writeln(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return writeln_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        auto writeln(const __FMT__::string_view&& str) {
            return writeln_impl(_desc, std::forward<const __FMT__::string_view>(str));
        }

        auto writeln(const std::FILE* file, const __FMT__::string_view&& str) {
            return writeln_impl(file, std::forward<const __FMT__::string_view>(str));
        }

        auto writeln() {
            return writeln_impl(_desc, "");
        }

        auto writeln(const std::FILE* file) {
            return writeln_impl(file, "");
        }

        template <class... Args>
        auto write(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return write_impl(_desc, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        auto write(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return write_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        auto write(const __FMT__::string_view&& str) {
            return write_impl(_desc, std::forward<const __FMT__::string_view>(str));
        }

        auto write(const std::FILE* file, const __FMT__::string_view&& str) {
            return write_impl(file, std::forward<const __FMT__::string_view>(str));
        }

        std::FILE* _desc = stdout;
    };

    /**
     * The operations are powered by standard io functions
     */
    class file_busy : public file {

        template <class... Args>
        static void write_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            std::string buff;
            buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("write failed: ") + strerror(errno));
        }

        template <class... Args>
        static void writeln_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            std::string buff;
            buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("write failed: ") + strerror(errno));
        }

        static void write_impl(const std::FILE* file, const __FMT__::string_view&& str) {
            std::string buff;
            buff = std::string(std::forward<const __FMT__::string_view>(str));
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("write failed: ") + strerror(errno));
        }

        static void writeln_impl(const std::FILE* file, const __FMT__::string_view&& str) {
            std::string buff;
            buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
            if (write(file->_fileno, buff.data(), buff.size()) < 0)
                throw std::runtime_error(std::string("write failed: ") + strerror(errno));
        }

    public:

        file_busy() = default;

        [[nodiscard]] static std::string input() {
            std::string s;
            std::cin >> s;
            return s;
        }

        template <class... Args>
        auto writeln(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return writeln_impl(_desc, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        auto writeln(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return writeln_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        auto writeln(const __FMT__::string_view&& str) {
            return writeln_impl(_desc, std::forward<const __FMT__::string_view>(str));
        }

        auto writeln(const std::FILE* file, const __FMT__::string_view&& str) {
            return writeln_impl(file, std::forward<const __FMT__::string_view>(str));
        }

        auto writeln() {
            return writeln_impl(_desc, "");
        }

        auto writeln(const std::FILE* file) {
            return writeln_impl(file, "");
        }

        template <class... Args>
        auto write(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return write_impl(_desc, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        auto write(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return write_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        auto write(const __FMT__::string_view&& str) {
            return write_impl(_desc, std::forward<const __FMT__::string_view>(str));
        }

        auto write(const std::FILE* file, const __FMT__::string_view&& str) {
            return write_impl(file, std::forward<const __FMT__::string_view>(str));
        }
    };

    /**
     * @brief The operations are attached to a current worker(runner) for processing. Useless for single thread app
     * @warning @c co_await operator required
     */
    class file_attached : public file {

        template <bool new_line = false>
        struct ACE_AWAIT_NODISCARD write_impl : io_query<write_impl<new_line>> {

            IMPORT_IO_QUERY_ENV(write_impl);

            write_impl() = delete;

            template <typename ... Args>
            explicit write_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args)
                : io_query_t(file->_fileno) {
                if constexpr (new_line)
                    _buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
                else
                    _buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            }

            explicit write_impl(const std::FILE* file, const __FMT__::string_view&& str)
                : io_query_t(file->_fileno) {
                if constexpr (new_line)
                    _buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
                else
                    _buff = std::string(std::forward<const __FMT__::string_view>(str));
            }

            bool setup_query(modules::kernel_observer* kwp) {
                lazy_write_observer* observer_ptr;
                if (not _observers_pool.capture(observer_ptr))
                    return modules::kernel_controller::write(kwp, _fd, _buff.data(), _buff.size(), 0);
                io_query_t::_is_silent = true;
                observer_ptr->_runner_identity = this->_runner_identity;
                observer_ptr->_buffer.assign(_buff.begin(), _buff.end());
                return modules::kernel_controller::write(observer_ptr, _fd,
                    observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0);
            }

            void await_resume() const { }

            std::string _buff;
        };

        typedef write_impl<true> writeln_impl;

    public:

        file_attached() = default;

        template <class... Args>
        auto writeln(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return writeln_impl(_desc, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        auto writeln(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return writeln_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        auto writeln(const __FMT__::string_view&& str) {
            return writeln_impl(_desc, std::forward<const __FMT__::string_view>(str));
        }

        auto writeln(const std::FILE* file, const __FMT__::string_view&& str) {
            return writeln_impl(file, std::forward<const __FMT__::string_view>(str));
        }

        auto writeln() {
            return writeln_impl(_desc, "");
        }

        auto writeln(const std::FILE* file) {
            return writeln_impl(file, "");
        }

        template <class... Args>
        auto write(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return write_impl(_desc, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        auto write(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return write_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        auto write(const __FMT__::string_view&& str) {
            return write_impl(_desc, std::forward<const __FMT__::string_view>(str));
        }

        auto write(const std::FILE* file, const __FMT__::string_view&& str) {
            return write_impl(file, std::forward<const __FMT__::string_view>(str));
        }
    };

}

#undef std

#endif //ACE_FS_H
