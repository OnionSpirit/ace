#ifndef ACE_FS_H
#define ACE_FS_H

#include <filesystem>
#include <list>
#include <utility>
#include <ace/core/async.h>
#include <ace/core/io.h>


namespace ace::fs {

    struct file_link;

    struct file;

}


    struct ace::fs::file_link : core::io_link {

        IMPORT_IO_LINK_ENV(file_link);
        IMPORT_IO_LINK_FABRICATION;

    protected:

        void output_action(const std::span<const char> buff) override {
            // NOTE: Trying to get current runner.
            // NOTE: Doing it manually for cases when classic 'runner::run()' is unused
            auto* runner_identity = core::runner::get().as<runner_pool_t>();
            // NOTE: Pushing data to slot, and setting identity for kernelic
            if (core::io_hanged::command* cmd; runner_identity and core::io_hanged::_command_pool.capture(cmd)) [[likely]]
            {
                cmd->_runner_identity = runner_identity;
                cmd->_buffer.assign(buff.begin(), buff.end());
                if (not core::services::kernel_controller::write(cmd, _fd,
                    cmd->_buffer.data(), cmd->_buffer.size(), 0) and core::io_hanged::fail_cb_handler)
                    core::io_hanged::fail_cb_handler(EAGAIN); // Maybe EIO?
            }
            // NOTE: If can not get slot or identity not found -> using busy behavior
            else
            {
                if (::write(_fd, buff.data(), buff.size()) < 0 and core::io_hanged::fail_cb_handler)
                    core::io_hanged::fail_cb_handler(errno);
            }
        };

        promise<int> input_action(void *buff, const std::size_t len) override {
            co_return co_await core::read_query(_fd, buff, len);
        }

    public:

        file_link() = default;

    };


    template<>
    struct ace::core::io_caster<ace::fs::file> {

        static auto as_link(int fd, bool is_closed, fs::file&&) {
            return fs::file_link { fd, is_closed };
        }
    };


    struct ace::fs::file : core::io_entity<file> {

        IMPORT_IO_ENTITY_ENV(file);

        std::filesystem::path _path;

        file(std::filesystem::path path)
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

            bool setup_query(kernel_observer* kwp) const noexcept {
                return core::services::kernel_controller::open(kwp, _path, _flags, _mode);
            }

            [[nodiscard]] auto await_resume() const {
                _entity._fd = _res;
                return core::io_link::consume(_entity);
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
