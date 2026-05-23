#ifndef ACE_FS_H
#define ACE_FS_H

#include <filesystem>
#include <list>
#include <utility>
#include <ace/core/async.h>
#include <ace/core/io.h>
#include <ace/futures/get_runner.h>

// NOTE: It is needed to use external fmt lib with older standards which does not support std::format
#ifndef __FMT__
#define __FMT__ std
#endif

namespace ace::fs {

    struct impl {

        struct lazy_write_observer : core::services::kernel_observer {

            std::vector<uint8_t> _buffer{};

            void on_result(const int res) override {
                _observers_pool.raw_sync(this);
            }

            ~lazy_write_observer() override = default;
        };

        static constexpr int buff_len = 256;

        static thread_local nukes::dynamic::reg_freelist<lazy_write_observer> _observers_pool;

        class lazy {

            lazy() = default;

        public:

            template <class... Args>
            static void write_impl(const int file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
                lazy_write_observer* observer_ptr;
                if (not _observers_pool.capture(observer_ptr))
                    if (write(file, buff.data(), buff.size()) < 0)
                        throw std::runtime_error(std::string("write failed: ") + strerror(errno));
                observer_ptr->_runner_identity = nullptr;
                observer_ptr->_buffer.assign(buff.begin(), buff.end());
                if (not core::services::kernel_controller::write(observer_ptr, file,
                    observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                    throw std::runtime_error("write failed");
            }

            template <class... Args>
            static void writeln_impl(const int file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
                lazy_write_observer* observer_ptr;
                if (not _observers_pool.capture(observer_ptr))
                    if (write(file, buff.data(), buff.size()) < 0)
                        throw std::runtime_error(std::string("write failed: ") + strerror(errno));
                observer_ptr->_runner_identity = nullptr;
                observer_ptr->_buffer.assign(buff.begin(), buff.end());
                if (not core::services::kernel_controller::write(observer_ptr, file,
                    observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                    throw std::runtime_error("write failed");
            }

            static void write_impl(const int file, const __FMT__::string_view&& str) {
                const std::string buff = std::string(std::forward<const __FMT__::string_view>(str));
                lazy_write_observer* observer_ptr;
                if (not _observers_pool.capture(observer_ptr))
                    if (write(file, buff.data(), buff.size()) < 0)
                        throw std::runtime_error(std::string("write failed: ") + strerror(errno));
                observer_ptr->_runner_identity = nullptr;
                observer_ptr->_buffer.assign(buff.begin(), buff.end());
                if (not core::services::kernel_controller::write(observer_ptr, file,
                    observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                    throw std::runtime_error("write failed");
            }

            static void writeln_impl(const int file, const __FMT__::string_view&& str) {
                const std::string buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
                lazy_write_observer* observer_ptr;
                if (not _observers_pool.capture(observer_ptr))
                    if (write(file, buff.data(), buff.size()) < 0)
                        throw std::runtime_error(std::string("write failed: ") + strerror(errno));
                observer_ptr->_runner_identity = nullptr;
                observer_ptr->_buffer.assign(buff.begin(), buff.end());
                if (not core::services::kernel_controller::write(observer_ptr, file,
                    observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0))
                    throw std::runtime_error("write failed");
            }
        };

        /**
         * The operations are powered by standard io functions
         */
        class busy {

            busy() = default;

        public:

            template <class... Args>
            static void write_impl(const int file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                std::string buff;
                buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
                if (write(file, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("write failed: ") + strerror(errno));
            }

            template <class... Args>
            static void writeln_impl(const int file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
                std::string buff;
                buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
                if (write(file, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("write failed: ") + strerror(errno));
            }

            static void write_impl(const int file, const __FMT__::string_view&& str) {
                std::string buff;
                buff = std::string(std::forward<const __FMT__::string_view>(str));
                if (write(file, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("write failed: ") + strerror(errno));
            }

            static void writeln_impl(const int file, const __FMT__::string_view&& str) {
                std::string buff;
                buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
                if (write(file, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("write failed: ") + strerror(errno));
            }

        };

        /**
         * @brief The operations are attached to a current worker(runner) for processing. Useless for single thread app
         * @warning @c co_await operator required
         */
        class pinned {

            pinned() = default;

        public:

            template <bool new_line = false>
            struct ACE_AWAIT_NODISCARD write_impl : core::io_query<write_impl<new_line>> {

                IMPORT_IO_QUERY_ENV(write_impl);

                write_impl() = delete;

                template <typename ... Args>
                explicit write_impl(const int file, __FMT__::format_string<Args...>&& fmt, Args&&... args)
                    : io_query_t(file) {
                    if constexpr (new_line)
                        _buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
                    else
                        _buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
                }

                explicit write_impl(const int file, const __FMT__::string_view&& str)
                    : io_query_t(file) {
                    if constexpr (new_line)
                        _buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
                    else
                        _buff = std::string(std::forward<const __FMT__::string_view>(str));
                }

                bool setup_query(core::services::kernel_observer* kwp) {
                    lazy_write_observer* observer_ptr;
                    if (not _observers_pool.capture(observer_ptr))
                        return core::services::kernel_controller::write(kwp, _fd, _buff.data(), _buff.size(), 0);
                    io_query_t::_is_silent = true;
                    observer_ptr->_runner_identity = this->_runner_identity;
                    observer_ptr->_buffer.assign(_buff.begin(), _buff.end());
                    return core::services::kernel_controller::write(observer_ptr, _fd,
                        observer_ptr->_buffer.data(), observer_ptr->_buffer.size(), 0);
                }

                void await_resume() const { }

                std::string _buff;
            };

            typedef write_impl<true> writeln_impl;

        };

    };

    thread_local nukes::dynamic::reg_freelist<impl::lazy_write_observer> impl::_observers_pool {};

    struct file_link : core::io_entity<file_link> {

        IMPORT_RAW_IO_ENTITY_ENV(file_link);

        file_link() = default;

        template <class... Args>
        auto writeln(__FMT__::format_string<Args...>&& fmt, Args&&... args)
        { return impl::lazy::writeln_impl(_fd, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...); }

        auto writeln(__FMT__::string_view&& str)
        { return impl::lazy::writeln_impl(_fd, std::forward<const __FMT__::string_view>(str)); }

        auto writeln()
        { return impl::lazy::writeln_impl(_fd, ""); }

        template <class... Args>
        auto write(__FMT__::format_string<Args...>&& fmt, Args&&... args)
        { return impl::lazy::write_impl(_fd, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...); }

        auto write(const __FMT__::string_view&& str)
        { return impl::lazy::write_impl(_fd, std::forward<const __FMT__::string_view>(str)); }

    };


    struct file : core::io_entity<file> {

        IMPORT_RAW_IO_ENTITY_ENV(file);

        std::filesystem::path _path;

        explicit file(std::filesystem::path path)
            : _path(std::move(path)) {};

        struct open_query : core::io_query<open_query> {

            IMPORT_IO_QUERY_ENV(open_query)

            open_query() = delete;

            explicit open_query(file&& entity, const char* path, const int flags, const mode_t mode)
                : io_query_t(0)
                , _entity(entity)
                , _path(path)
                , _flags(flags)
                , _mode(mode) {}

            bool setup_query(core::services::kernel_observer* kwp) const noexcept {
                return core::services::kernel_controller::open(kwp, _path, _flags, _mode);
            }

            [[nodiscard]] auto await_resume() const {
                return file_link::consume(_entity);
            }

            file& _entity;
            const char* _path;
            const int _flags;
            const mode_t _mode;
        };

        ACE_AWAIT_NODISCARD auto open_impl(const int flags, const mode_t mode)
        { return open_query { std::move(*this), _path.c_str(), flags, mode}; }

        ACE_AWAIT_NODISCARD auto open(const int flags = O_CREAT | O_APPEND | O_RDWR, const mode_t mode = S_IRWXO)
        -> open_query { return open_query { std::move(*this), _path.c_str(), flags, mode }; }

        ACE_AWAIT_NODISCARD auto open_rewrite()
        -> open_query { return open_query { std::move(*this), _path.c_str(), O_CREAT | O_RDWR, S_IRWXO }; }

        ACE_AWAIT_NODISCARD auto open_rdonly()
        -> open_query { return open_query { std::move(*this), _path.c_str(), O_CREAT | O_RDONLY, S_IRWXO }; }

        ACE_AWAIT_NODISCARD auto open_wronly()
        -> open_query { return open_query { std::move(*this), _path.c_str(), O_CREAT | O_APPEND | O_WRONLY, S_IRWXO }; }

    };

}

#undef std

#endif //ACE_FS_H
