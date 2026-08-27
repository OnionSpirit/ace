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
#include <ace/io.h>


namespace ace::fs {

    /**
     * @brief @c io_link for open files — async read/write via @c io_uring.
     *
     * @details @c output_action() writes via @c io::outcast (fallback to
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
     * @c io::outcast::command, with a blocking @c ::write() fallback only
     * when asynchronous dispatch is unavailable for reasons other than an
     * @c io_uring initialization failure.
     * @c input_action() uses @c core::read_query for async reads.
     */
    struct ace::fs::file_link : io::link {

        IMPORT_IO_LINK_ENV(file_link);
        IMPORT_IO_LINK_FABRICATION;

    protected:

        /**
         * @brief Writes a scatter-gather buffer to the file asynchronously.
         * @details Tries to capture an @c io::outcast::command and submit a
         * @c writev operation through @c kernel_controller; falls back to a
         * blocking @c ::writev() when no runner context or command slot is
         * available. An unavailable @c io_uring instead returns the command to
         * its pool and reports the exact initialization error through
         * @c io::outcast::fail_cb_handler.
         * @param buff Buffer to write.
         */
        void output_action(io::buffer&& buff) override {
            // NOTE: Trying to get current runner.
            // NOTE: Doing it manually for cases when classic 'runner::run()' is unused
            auto* runner_identity = core::runner::get().as<runner_pool_t>();
            // NOTE: Pushing data to slot, and setting identity for kernelic
            if (io::outcast::command* cmd {}; runner_identity and io::outcast::_command_pool.capture(cmd)) [[likely]] {
                cmd->_runner_identity = runner_identity;
                cmd->_buffer = std::move(buff);
                cmd->_description = "fs::file_link lazy-write";
                const auto* assembled = cmd->_buffer.assemble();
                if (services::kernel_controller::writev(cmd, _fd, assembled->msg_iov, assembled->msg_iovlen, 0, 0))
                    return;
                const int error = services::kernel_controller::initialization_error();
                if (error not_eq 0) {
                    // No CQE will arrive after a failed init, so complete the
                    // command locally through its normal result path.
                    cmd->on_result(error);
                    return;
                }
            }
            // NOTE: If can not get slot or identity not found -> using busy behavior
            const auto* assembled = buff.assemble();
            if (::writev(_fd, assembled->msg_iov, static_cast<int>(assembled->msg_iovlen)) < 0 and io::outcast::fail_cb_handler)
                io::outcast::fail_cb_handler(errno, "fs::file_link busy-write");
        };

        /**
         * @brief Reads data from the file into a buffer asynchronously.
         * @param buff Destination buffer.
         * @param len  Number of bytes to read.
         * @return Awaitable resolving to the number of bytes read.
         */
        promise<int> input_action(void *buff, const std::size_t len) override {
            co_return co_await io::read_query(_fd, buff, len);
        }

    public:

        /// @brief Default constructor — produces an empty link.
        file_link() = default;

    };


    /**
     * @brief Specialisation of @c io::caster for @c ace::fs::file.
     * @details Converts a consumed file entity into a ready-to-use @c file_link.
     */
    template<>
    struct ace::io::caster<ace::fs::file> {

        /**
         * @brief Builds a @c file_link from a file entity's descriptor.
         * @param fd        File descriptor of the opened file.
         * @param is_closed Whether the descriptor is considered closed.
         * @return The constructed @c file_link.
         */
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

        /// @brief Filesystem path of the file to open.
        std::filesystem::path _path;

        /**
         * @brief Constructs a file entity for the given path.
         * @param path Path of the file.
         */
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

            /**
             * @brief Consumes the entity with the opened descriptor.
             * @return A @c file_link ready for I/O.
             */
            [[nodiscard]] auto await_resume() const {
                _entity._fd = _res;
                return io::link::consume(_entity);
            }

            file& _entity;      ///< Entity being consumed by the open operation.
            const char* _path;  ///< Path of the file to open.
            const int _flags;   ///< Open flags (O_CREAT, O_RDWR, ...).
            const mode_t _mode; ///< Permission bits for newly created files.
        };

        /**
         * @brief Opens the file with custom flags and mode.
         * @param flags Open flags.
         * @param mode  Permission bits for newly created files.
         * @return Awaitable resolving to a @c file_link.
         */
        ACE_AWAIT_NODISCARD auto open_impl(const int flags, const mode_t mode)
        { return open_query { std::move(*this), _path.c_str(), flags, mode}; }

        /**
         * @brief Opens the file for append + read/write, creating it if missing.
         * @param flags Open flags (default: O_CREAT | O_APPEND | O_RDWR).
         * @param mode  Permission bits (default: 0777).
         * @return Awaitable resolving to a @c file_link.
         */
        ACE_AWAIT_NODISCARD auto open(const int flags = O_CREAT | O_APPEND | O_RDWR, const mode_t mode = 0777)
        -> open_query { return open_query { std::move(*this), _path.c_str(), flags, mode }; }

        /**
         * @brief Opens the file for rewrite, creating it if it does not exist.
         * @details Uses @c O_CREAT | @c O_TRUNC | @c O_RDWR, so an existing
         * file is truncated before read/write access. Newly created files use
         * mode @c 0777, subject to the process umask.
         * @return Awaitable that consumes this entity and resolves to the
         * opened @c file_link.
         */
        ACE_AWAIT_NODISCARD auto open_rewrite()
        -> open_query { return open_query { std::move(*this), _path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0777 }; }

        /**
         * @brief Opens the file read-only.
         * @return Awaitable resolving to a @c file_link.
         */
        ACE_AWAIT_NODISCARD auto open_rdonly()
        -> open_query { return open_query { std::move(*this), _path.c_str(), O_RDONLY, 0777 }; }

        /**
         * @brief Opens the file for append + write-only, creating it if missing.
         * @return Awaitable resolving to a @c file_link.
         */
        ACE_AWAIT_NODISCARD auto open_wronly()
        -> open_query { return open_query { std::move(*this), _path.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0777 }; }

    };

#endif //ACE_FS_H
