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

    struct writer {

        struct command : core::services::kernel_observer {

            std::vector<uint8_t> _buffer{};

            void on_result(const int res) override try {
                if (res < 0) throw std::runtime_error(std::string("print failed: ") + strerror(-res));
                _command_pool.raw_sync(this);
            } catch (std::exception& e) { std::cerr << e.what() << std::endl; }

            ~command() override = default;
        };

        static constexpr int buff_len = 256;

        static thread_local nukes::dynamic::reg_freelist<command> _command_pool;

        /**
         * @brief Choosing way of writing
         * @param [in] file file to write to
         * @param [in] buff data to write
         */
        static void write_dispatch(const int file, const __FMT__::string_view buff) try {
            // NOTE: Trying to get thread local runner from the dispatcher
            auto* runner_identity = reinterpret_cast<runner_pool_t*>(core::dispatcher::get_local_runner());
            // NOTE: If can not get slot or identity not found -> using busy behavior
            if (command* cmd; not _command_pool.capture(cmd) or not runner_identity) [[unlikely]] {
                if (write(file, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("print failed: ") + strerror(errno));
            }
            // NOTE: Pushing data to slot, and setting identity for kernelic
            else [[likely]] {
                cmd->_runner_identity = runner_identity;
                cmd->_buffer.assign(buff.begin(), buff.end());
                if (not core::services::kernel_controller::write(cmd, file,
                    cmd->_buffer.data(), cmd->_buffer.size(), 0))
                    throw std::runtime_error("print failed: Ring buffering failed");
            }
        } catch (std::exception& e) { std::cerr << e.what() << std::endl; }

        template <class... Args>
        static void write_impl(const int file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            write_dispatch(file, buff);
        }

        template <class... Args>
        static void writeln_impl(const int file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
            write_dispatch(file, buff);
        }

        static void write_impl(const int file, const __FMT__::string_view&& str) {
            const std::string buff = std::string(std::forward<const __FMT__::string_view>(str));
            write_dispatch(file, buff);
        }

        static void writeln_impl(const int file, const __FMT__::string_view&& str) {
            const std::string buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
            write_dispatch(file, buff);
        }

    };

    thread_local nukes::dynamic::reg_freelist<writer::command> writer::_command_pool {};

    struct file_link : core::io_entity<file_link> {

        IMPORT_RAW_IO_ENTITY_ENV(file_link);

        file_link() = default;

        template <class... Args>
        auto writeln(__FMT__::format_string<Args...>&& fmt, Args&&... args)
        { return writer::writeln_impl(_fd, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...); }

        auto writeln(__FMT__::string_view&& str) const
        { return writer::writeln_impl(_fd, std::forward<const __FMT__::string_view>(str)); }

        auto writeln() const
        { return writer::writeln_impl(_fd, ""); }

        template <class... Args>
        auto write(__FMT__::format_string<Args...>&& fmt, Args&&... args)
        { return writer::write_impl(_fd, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...); }

        auto write(const __FMT__::string_view&& str) const
        { return writer::write_impl(_fd, std::forward<const __FMT__::string_view>(str)); }

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
