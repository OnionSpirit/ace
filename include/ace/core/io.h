#ifndef ACE_IO_H
#define ACE_IO_H

#include "kernelic.h"

namespace ace::core {

    template <typename query_t>
    concept is_query = requires(query_t q, kernel_observer* kwp) {
        { q.setup_query(kwp) } -> std::same_as<bool>;
    };

    template <typename entry_t>
    concept is_entity = requires(entry_t q) {
        { q._fd } -> std::same_as<int>;
        { q._is_closed } -> std::same_as<bool>;
    };

    template <typename entry_t>
    concept is_entry = requires(entry_t q) {
        { q.clear() } -> std::same_as<void>;
    };

    /**
     * @brief An interface to interact with the
     * @c ace::core::kernel_controller via @c co_await operator.
     * <br>Does not define 'resume(...)' logic.
     * @tparam query_core_t Specific query type.
     */
    template <typename query_core_t>
    struct io_query : futures::future_traits<query_core_t>, kernel_observer {

        IMPORT_FUTURE_ENV(query_core_t);

        explicit io_query(const int fd) : _fd(fd) {
            static_assert(is_query<query_core_t>,
                "Query object shall implement 'bool setup_query(ace::core::kernel_waiter*)' method");
        }

        struct io_socket_query_conductor : conductor_handler_t {

            io_socket_query_conductor() = delete;

            explicit io_socket_query_conductor(io_query* query_)
                : _query(query_) {};

            void forward(task&& ctx) override {
                _query->_waiter = std::move(ctx);
            }

            void cancel() override {
                // TODO: Improve cancel with pop from local submission queue
                kernel_controller::cancel(_query, 0);
            }

            ~io_socket_query_conductor() override = default;

            io_query* _query;
        };

        task _waiter;    ///< Awaited task storage
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
            if (static_cast<query_core_t*>(this)->setup_query(this)) {
                coroutine.promise()._runner_conductor = io_socket_query_conductor{this};
                return true;
            }
            return false;
        }

        void on_result(const int res) override {
            _res = res;
            runner::reattach(std::move(_waiter));
        }

        ~io_query() override = default;
    };

#define IMPORT_IO_QUERY_ENV(class)                    \
    typedef ace::core::io_query<class> io_query_t;    \
    using io_query_t::_fd;                            \
    using io_query_t::_res;                           \
    ~class() override = default;


    struct read_query : io_query<read_query> {

        read_query() = delete;

        [[nodiscard]] explicit read_query(const int fd, void *buf, const unsigned nbytes, const uint64_t offset = 0)
            : io_query(fd)
            , _fd(fd)
            , _buf(buf)
            , _nbytes(nbytes)
            , _offset(offset) {}

        bool setup_query(kernel_observer* kwp) const {
            return kernel_controller::read(kwp, _fd, _buf, _nbytes, _offset);
        }

        [[nodiscard]] int await_resume() const {
            // NOTE: Nul-termination for input
            if (_res > 0) static_cast<char*>(_buf)[_res] = '\0';
            return _res;
        }

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

        bool setup_query(kernel_observer* kwp) const {
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

        bool setup_query(kernel_observer* kwp) const noexcept {
            return kernel_controller::close(kwp, _fd);
        }

        [[nodiscard]] int await_resume() const { return _res; }
    };

    /**
     * @brief Handler for a file descriptor with RAII guard behavior.
     * The io_entity derived types shall represent FD state by providing allowed async operations depending on FD state.
     */
    template <typename entity_t, typename ... Params>
    struct io_entity {

        io_entity()
            : _fd(-1)
            , _is_closed(true) {}

        io_entity(const int fd, const bool is_closed, std::tuple<Params...> params)
            : _fd(fd)
            , _is_closed(is_closed)
            , _params(params) { };

        template<typename entry_t>
        static entity_t consume(entry_t& io) noexcept {
            int fd = io._fd;
            bool is_closed;
            if (fd > -1) is_closed = io._is_closed;
            else is_closed = true;
            auto params = std::move(io._params);
            io._is_closed = true;
            io._fd = -1;
            return entity_t {fd, is_closed, params};
        }

        io_entity(io_entity&& io) noexcept {
            _fd = io._fd;
            _is_closed = io._is_closed;
            _params = std::move(io._params);
            io._fd = -1;
            io._is_closed = true;
        }

        io_entity& operator=(io_entity&& io)  noexcept {
            _fd = io._fd;
            _is_closed = io._is_closed;
            _params = std::move(io._params);
            io._fd = -1;
            io._is_closed = true;
            return *this;
        }

        [[nodiscard]] auto close()
            -> core::close_query { _is_closed = true; return core::close_query{_fd}; }

        virtual ~io_entity() = default;

    protected:

        int  _fd;                      ///< Socket file descriptor
        bool _is_closed;               ///< Socket closed flag
        std::tuple<Params...> _params; ///< FD related params

    private:

        /**
         * @brief RAII io guard
         */
        struct io_guard final {
            io_guard() = delete;
            explicit io_guard(const int& fd, const bool& closed)
                : _fd(fd)
                , _closed(closed) {}

            const int& _fd;
            const bool& _closed;

            static task check_and_close(const bool closed, const int fd) noexcept {
                if (not closed) {
                    const int res = co_await core::close_query{fd};
                    if (res < 0) std::cerr << strerror(res) << std::endl;
                }
            }

            static void pending_close(const bool closed, const int fd) noexcept {
                schedule(check_and_close(closed, fd));
            }

            ~io_guard() noexcept { pending_close(_closed, _fd); }
        };

        io_guard _guard {_fd, _is_closed};
    };

#define IMPORT_ERROR_HANDLING                                                               \
                                                                                            \
    operator bool() const { return _fd > -1 or INT_MIN == _fd; }                            \
                                                                                            \
    std::string_view error() const {                                                        \
        if (_fd > -1)                                                                       \
            throw std::logic_error("can not receive 'error()' on successed 'io_entity'");   \
        if (INT_MIN == _fd)                                                                 \
            throw std::logic_error("can not receive 'error()' on idle 'io_entry'");         \
        return strerror(_fd);                                                               \
    }

#define IMPORT_IO_ENTITY_ENV(class, ...)                                                    \
                                                                                            \
    typedef ace::core::io_entity<class, __VA_ARGS__> io_entity_t;                           \
                                                                                            \
    class(const int fd, const bool is_closed, std::tuple<__VA_ARGS__> params)               \
        : io_entity_t(fd, is_closed, params) { };                                           \
                                                                                            \
    using io_entity_t::_fd;                                                                 \
    using io_entity_t::_is_closed;                                                          \
    using io_entity_t::_params;                                                             \
                                                                                            \
    class(class&& io) noexcept                                                              \
        : io_entity_t(static_cast<io_entity_t>(std::move(io))) { }                          \
                                                                                            \
    class& operator = (class&& io) noexcept {                                               \
        _fd = io._fd;                                                                       \
        _is_closed = io._is_closed;                                                         \
        _params = std::move(io._params);                                                    \
        io._fd = -1;                                                                        \
        io._is_closed = true;                                                               \
        return *this;                                                                       \
    }                                                                                       \
                                                                                            \
    IMPORT_ERROR_HANDLING                                                                   \
                                                                                            \
    ~class() override = default;

}

#endif //ACE_IO_H
