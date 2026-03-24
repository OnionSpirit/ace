#ifndef ACE_IO_H
#define ACE_IO_H

#include "kernelic.h"

namespace ace::core {

    template <typename query_t>
    concept is_query = requires(query_t q, kernel_waiter* kwp) {
        { q.query(kwp) } -> std::same_as<bool>;
    };

    /**
     * @brief An interface to interact with the
     * @b ace::core::kernel_controller via @b co_await operator.
     * <br>Does not define 'resume(...)' logic.
     * @tparam query_core_t Specific query type.
     */
    template <typename query_core_t>
    struct io_query : futures::future_traits<query_core_t>, kernel_waiter {

        DECLARE_FUTURE(query_core_t);
        IMPORT_FUTURE_ENV;

        explicit io_query(const int fd) : _fd(fd) {
            static_assert(is_query<query_core_t>,
                "Query object shall implement 'bool query(ace::core::kernel_waiter*)' method");
        }

        struct io_socket_query_conductor : conductor_handler_t {

            io_socket_query_conductor() = delete;

            explicit io_socket_query_conductor(io_query* query_)
                : _query(query_) {};

            void forward(async<>&& ctx) override {
                _query->_waiter = std::move(ctx);
            }

            void cancel() override {
                // TODO: Improve cancel with pop from local submission queue
                kernel_controller::cancel(_query, 0);
            }

            ~io_socket_query_conductor() override = default;

            io_query* _query;
        };

        async<> _waiter;    ///< Awaited task storage
        int _res = INT_MIN;
        const int _fd;

        bool await_ready() override { return false; };

        bool await_suspend(auto coroutine) {
            if (_fd < 0)
                throw std::logic_error("Trying to make query on failed 'io_entity' [Query object type: "
                    + std::string{typeid(query_core_t).name()} + "]");
            if (INT_MIN == _fd)
                throw std::logic_error("Trying to make query on idle 'io_entry' [Query object type: "
                    + std::string{typeid(query_core_t).name()} + "]");
            if (static_cast<query_core_t*>(this)->query(this)) {
                coroutine.promise()._runner_conductor = io_socket_query_conductor{this};
                return true;
            }
            return false;
        }

        void activate(const int res) override {
            _res = res;
            _waiter.release_future();
            core::runner::reattach(std::move(_waiter));
        }

        ~io_query() override = default;
    };

#define IMPORT_IO_QUERY_ENV(class)                 \
    typedef ace::core::io_query<class> io_query_t; \
    using io_query_t::_fd;                         \
    using io_query_t::_res;


    struct read_query : io_query<read_query> {

        read_query() = delete;

        explicit read_query(const int fd, void *buf, const unsigned nbytes, const uint64_t offset = 0)
            : io_query(fd)
            , _fd(fd)
            , _buf(buf)
            , _nbytes(nbytes)
            , _offset(offset) {}

        bool query(kernel_waiter* kwp) const {
            return kernel_controller::read(kwp, _fd, _buf, _nbytes, _offset);
        }

        [[nodiscard]] int await_resume() const { return _res; }

        const int _fd;
        void *_buf;
        const unsigned _nbytes;
        const uint64_t _offset;
    };


    struct write_query : io_query<write_query> {

        write_query() = delete;

        explicit write_query(const int fd, const void *buf, const unsigned nbytes, const uint64_t offset = 0)
            : io_query(fd)
            , _fd(fd)
            , _buf(buf)
            , _nbytes(nbytes)
            , _offset(offset) {}

        bool query(kernel_waiter* kwp) const {
            return ace::core::kernel_controller::write(kwp, _fd, _buf, _nbytes, _offset);
        }

        [[nodiscard]] int await_resume() const { return _res; }

        const int _fd;
        const void *_buf;
        const unsigned _nbytes;
        const uint64_t _offset;
    };


    struct close_query : io_query<close_query> {

        IMPORT_IO_QUERY_ENV(close_query)

        close_query() = delete;

        explicit close_query(const int fd) : io_query_t(fd) {}

        bool query(kernel_waiter* kwp) const noexcept {
            return kernel_controller::close(kwp, _fd);
        }

        [[nodiscard]] int await_resume() const { return _res; }
    };

}

#endif //ACE_IO_H
