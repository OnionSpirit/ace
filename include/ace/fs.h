/**
 * @file fs.h
 * @brief Asynchronous filesystem I/O — file entities and links on top of the
 *        ACE I/O framework.
 *
 * @details Provides @c ace::fs::file (an @c io_entity for regular files) and
 * @c ace::fs::file_link (an @c io_link for open files).  File opening is
 * asynchronous via @c open_query (built on @c io_uring @c io_uring_prep_open).
 *
 * @see ace::io::entity, ace::io::link
 */
#ifndef ACE_FS_H
#define ACE_FS_H

#include <filesystem>
#include <list>
#include <utility>
#include <ace/core/async.h>
#include <ace/core/io.h>


namespace ace::fs {

    /**
     * @brief @c io_link for open files — async read/write via @c io_uring.
     *
     * @details @c output_action() writes via @c io_hanged (fallback to
     * blocking @c ::write()).  @c input_action() reads via
     * @c core::read_query.
     */
    struct file_link;

    /**
     * @brief @c io_entity representing a regular file.
     *
     * @details Stores a @c std::filesystem::path and provides async
     * @c open() / @c open_rewrite() / @c open_rdonly() / @c open_wronly()
     * methods.  On successful open, the entity is consumed and produces a
     * @c file_link.
     */
    struct file;

}


    /**
     * @brief @c io_link for open files.
     *
     * @details Implements @c output_action() via async write through
     * @c io_hanged::command (with blocking @c ::write() fallback).
     * @c input_action() uses @c core::read_query for async reads.
     */
    struct ace::fs::file_link : io::link {

        IMPORT_IO_LINK_ENV(file_link);
        IMPORT_IO_LINK_FABRICATION;

    protected:

        void output_action(const std::span<const char> buff) override {
            // NOTE: Trying to get current runner.
            // NOTE: Doing it manually for cases when classic 'runner::run()' is unused
            auto* runner_identity = core::runner::get().as<runner_pool_t>();
            // NOTE: Pushing data to slot, and setting identity for kernelic
            if (io::hanged::command* cmd; runner_identity and io::hanged::_command_pool.capture(cmd)) [[likely]]
            {
                cmd->_runner_identity = runner_identity;
                cmd->_buffer.assign(buff.begin(), buff.end());
                if (not services::kernel_controller::write(cmd, _fd,
                    cmd->_buffer.data(), cmd->_buffer.size(), 0) and io::hanged::fail_cb_handler)
                    io::hanged::fail_cb_handler(EAGAIN); // Maybe EIO?
            }
            // NOTE: If can not get slot or identity not found -> using busy behavior
            else
            {
                if (::write(_fd, buff.data(), buff.size()) < 0 and io::hanged::fail_cb_handler)
                    io::hanged::fail_cb_handler(errno);
            }
        };

        promise<int> input_action(void *buff, const std::size_t len) override {
            co_return co_await io::read_query(_fd, buff, len);
        }

    public:

        file_link() = default;

    };


    template<>
    struct ace::io::caster<ace::fs::file> {

        static auto as_link(int fd, bool is_closed, fs::file&&) {
            return fs::file_link { fd, is_closed };
        }
    };


    /**
     * @brief @c io_entity for regular files with async open operations.
     *
     * @details On construction, the file is in "idle" state (invalid FD).
     * Calling one of the @c open() variants returns an @c open_query
     * awaitable; on success, @c await_resume() consumes the entity and
     * returns a @c file_link ready for I/O.
     */
    struct ace::fs::file : io::entity<file> {

        IMPORT_IO_ENTITY_ENV(file);

        std::filesystem::path _path;

        file(std::filesystem::path path)
            : _path(std::move(path)) {};

        /**
         * @brief Awaitable query for opening a file via @c io_uring.
         *
         * @details Submits @c io_uring_prep_open to @c kernel_controller.
         * On success, @c await_resume() consumes the @c file entity and
         * returns a @c file_link.
         */
        struct open_query : io::query<open_query> {

            IMPORT_IO_QUERY_ENV(open_query)

            open_query() = delete;

            explicit open_query(file&& entity, const char* path, const int flags, const mode_t mode)
                : io_query_t(0)
                , _entity(entity)
                , _path(path)
                , _flags(flags)
                , _mode(mode) {}

            bool setup_query(services::kernel_observer* kwp) const noexcept {
                return services::kernel_controller::open(kwp, _path, _flags, _mode);
            }

            [[nodiscard]] auto await_resume() const {
                _entity._fd = _res;
                return io::link::consume(_entity);
            }

            file& _entity;
            const char* _path;
            const int _flags;
            const mode_t _mode;
        };

        ACE_AWAIT_NODISCARD auto open_impl(const int flags, const mode_t mode)
        { return open_query { std::move(*this), _path.c_str(), flags, mode}; }

        ACE_AWAIT_NODISCARD auto open(const int flags = O_CREAT | O_APPEND | O_RDWR, const mode_t mode = 0777)
        -> open_query { return open_query { std::move(*this), _path.c_str(), flags, mode }; }

        ACE_AWAIT_NODISCARD auto open_rewrite()
        -> open_query { return open_query { std::move(*this), _path.c_str(), O_CREAT | O_RDWR, 0777 }; }

        ACE_AWAIT_NODISCARD auto open_rdonly()
        -> open_query { return open_query { std::move(*this), _path.c_str(), O_RDONLY, 0777 }; }

        ACE_AWAIT_NODISCARD auto open_wronly()
        -> open_query { return open_query { std::move(*this), _path.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0777 }; }

    };

#endif //ACE_FS_H
