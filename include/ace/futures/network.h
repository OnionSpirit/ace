#ifndef ACE_NET_H
#define ACE_NET_H

#include <netinet/in.h>

#include "future.h"
#include "ace/core/kernelic.h"

namespace ace::futures {


    template <typename query_t>
    concept is_query = requires(query_t q, core::kernel_waiter* kwp) {
        { q.query(kwp) } -> std::same_as<bool>;
    };

    /**
     * @brief Base io_socket class with defined ownership and guard behavior
     */
    struct io_socket_base {

        int  _fd;         ///< Socket file descriptor
        bool _is_closed;  ///< Socket closed flag

        io_socket_base()
            : _fd(-1)
            , _is_closed(true) {}

        io_socket_base(const io_socket_base&) = delete;

        io_socket_base& operator=(const io_socket_base&) = delete;

        io_socket_base(io_socket_base&& io) noexcept {
            _fd = io._fd;
            _is_closed = io._is_closed;
            io._fd = -1;
            io._is_closed = true;
        }

        io_socket_base& operator=(io_socket_base&& io)  noexcept {
            _fd = io._fd;
            _is_closed = io._is_closed;
            io._fd = -1;
            io._is_closed = true;
            return *this;
        }

        /**
         * @brief An interface template to interact with the
         * @b ace::core::kernel_controller through @b co_await operator.
         * <br>Does not define 'resume(...)' logic.
         * @tparam query_t Specific query type.
         */
        template <typename query_t>
        struct basic_query_traits : future_traits<query_t>, core::kernel_waiter {

            DECLARE_FUTURE(query_t);
            IMPORT_FUTURE_ENV;

            explicit basic_query_traits(const int fd) : _fd(fd) {
                static_assert(is_query<query_t>,
                    "Query object shall implement 'bool query(ace::core::kernel_waiter*)' method");
            }

            struct io_socket_query_conductor : conductor_handler_t {

                io_socket_query_conductor() = delete;

                explicit io_socket_query_conductor(basic_query_traits* query_)
                    : _query(query_) {};

                void forward(async<>&& ctx) override {
                    _query->_waiter = std::move(ctx);
                }

                void cancel() override {
                    core::kernel_controller::cancel(_query, 0);
                }

                ~io_socket_query_conductor() override = default;

                basic_query_traits* _query;
            };

            async<> _waiter;
            int _res = -1;
            const int _fd;

            bool await_ready() override { return false; };

            bool await_suspend(auto coroutine) {
                if (_fd > -1 and static_cast<query_t*>(this)->query(this)) {
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

            ~basic_query_traits() override = default;
        };


        /**
         * @brief An interface template with 'resume(...)' logic to interact with the
         * @b ace::core::kernel_controller through @b co_await operator.
         * @tparam query_t Specific query type.
         */
        template <typename query_t>
        struct complete_query_traits : basic_query_traits<query_t> {

            typedef basic_query_traits<query_t> future_query_traits_t;
            using future_query_traits_t::_res;
            using future_query_traits_t::_waiter;

            explicit complete_query_traits(const int fd)
                : basic_query_traits<query_t>(fd) {}

            [[nodiscard]] int await_resume() const { return _res; }
        };

        struct close_query : complete_query_traits<close_query> {

            close_query() = delete;

            explicit close_query(const int fd) : complete_query_traits(fd) {}

            bool query(kernel_waiter* kwp) const noexcept {
                return core::kernel_controller::close(kwp, _fd);
            }
        };

        [[nodiscard]] auto close()
        -> close_query { _is_closed = true; return close_query{_fd}; }

        virtual ~io_socket_base() = default;

    private:

        /**
         * @brief RAII socket guard
         */
        struct io_socket_guard final {
            io_socket_guard() = delete;
            explicit io_socket_guard(const int& fd, const bool& closed)
                : _fd(fd)
                , _closed(closed) {}

            const int& _fd;
            const bool& _closed;

            static async<> check_and_close(const bool closed, const int fd) noexcept {
                if (not closed) {
                    const int res = co_await close_query{fd};
                    if (res < 0) std::cerr << res << std::endl;
                }
            }

            static void pending_close(const bool closed, const int fd) noexcept {
                schedule(check_and_close(closed, fd));
            }

            ~io_socket_guard() noexcept { pending_close(_closed, _fd); }
        };

        io_socket_guard _guard {_fd, _is_closed};
    };


    /**
     * @brief io_socket class that represents active connection
     */
    struct io_socket_connection : io_socket_base {

        io_socket_connection() = delete;

        explicit io_socket_connection(const int fd) {
            _fd = fd;
            if (_fd > -1) _is_closed = false;
        }

        struct send_query : complete_query_traits<send_query> {

            send_query() = delete;

            explicit send_query(const int fd, const void *buf, const size_t len, const int flags = 0)
                : complete_query_traits(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags) {}

            bool query(kernel_waiter* kwp) const {
                return core::kernel_controller::send(kwp, _fd, _buf, _len, _flags);
            }

            const void *_buf;
            const size_t _len;
            const int _flags;
        };

        struct sendto_query : complete_query_traits<sendto_query> {

            sendto_query() = delete;

            explicit sendto_query(const int fd, const void *buf, const size_t len, const int flags,
                const sockaddr *addr, const socklen_t addrlen)
                : complete_query_traits(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(kernel_waiter* kwp) const {
                return core::kernel_controller::sendto(kwp, _fd, _buf, _len, _flags, _addr, _addrlen);
            }

            const void *_buf;
            const size_t _len;
            const int _flags;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        struct recv_query : complete_query_traits<recv_query> {

            recv_query() = delete;

            explicit recv_query(const int fd, void *buf, const size_t len, const int flags = 0)
                : complete_query_traits(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags) {}

            bool query(kernel_waiter* kwp) const {
                return core::kernel_controller::recv(kwp, _fd, _buf, _len, _flags);
            }

            void *_buf;
            const size_t _len;
            const int _flags;
        };

        [[nodiscard]] auto send(const void *buf, const size_t len, const int flags = 0) const
        -> send_query { return send_query{_fd, buf, len, flags}; }

        [[nodiscard]] auto sendto(const void *buf, const size_t len, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query { return sendto_query{_fd, buf, len, flags, addr, addrlen}; }

        [[nodiscard]] auto recv(void *buf, const size_t len, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf, len, flags}; }

        ~io_socket_connection() override = default;
    };

    /**
     * @brief io_socket class that represents listen socket
     */
    struct io_socket_listener : io_socket_base  {

        io_socket_listener() = delete;

        explicit io_socket_listener(const int fd) {
            _fd = fd;
            if (_fd > -1) _is_closed = false;
        }

        struct accept_query : basic_query_traits<accept_query> {

            accept_query() = delete;

            explicit accept_query(const int fd, sockaddr* addr, socklen_t* addrlen, const int flags = 0)
                : basic_query_traits(fd)
                , _addr(addr)
                , _addrlen(addrlen)
                , _flags(flags) {}

            bool query(kernel_waiter* kwp) const {
                return core::kernel_controller::accept(kwp, _fd, _addr, _addrlen, _flags);
            }

            [[nodiscard]] io_socket_connection await_resume() const { return io_socket_connection{_res}; }

            sockaddr* _addr;
            socklen_t* _addrlen;
            const int _flags;
        };

        [[nodiscard]] auto accept(sockaddr* addr, socklen_t* addrlen, const int flags = 0) const
        -> accept_query { return accept_query{_fd, addr, addrlen, flags}; }

        ~io_socket_listener() override = default;
    };

    /**
     * @brief io_socket class that represents idle socket on initial state
     */
    struct io_socket_idle : io_socket_base {

        io_socket_idle() : io_socket_base() {};

        explicit io_socket_idle(const int fd) {
            _fd = fd;
            if (_fd > -1) _is_closed = false;
        }

        struct bind_query : complete_query_traits<bind_query> {

            bind_query() = delete;

            explicit bind_query(const int fd, const sockaddr* addr, const socklen_t addrlen)
                : complete_query_traits(fd)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(kernel_waiter* kwp) const {
                return core::kernel_controller::bind(kwp, _fd, _addr, _addrlen);
            }

            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        struct connect_query : basic_query_traits<connect_query> {

            connect_query() = delete;

            explicit connect_query(const int fd, const sockaddr* addr, const socklen_t addrlen)
                : basic_query_traits(fd)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(kernel_waiter* kwp) const {
                return core::kernel_controller::connect(kwp, _fd, _addr, _addrlen);
            }

            [[nodiscard]] io_socket_connection await_resume() const { return io_socket_connection{_res}; }

            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        struct listen_query : basic_query_traits<listen_query> {

            listen_query() = delete;

            explicit listen_query(const int fd, const int backlog)
                : basic_query_traits(fd)
                , _backlog(backlog) {}

            bool query(kernel_waiter* kwp) const {
                return core::kernel_controller::listen(kwp, _fd, _backlog);
            }

            [[nodiscard]] io_socket_listener await_resume() const { return io_socket_listener{_res}; }

            const int _backlog;
        };

        [[nodiscard]] auto bind(const sockaddr* addr, const socklen_t addrlen) const
        -> bind_query { return bind_query {_fd, addr, addrlen}; }

        [[nodiscard]] auto listen(const int backlog) const
        -> listen_query { return listen_query{_fd, backlog}; }

        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen) const
        -> connect_query { return connect_query{_fd, addr, addrlen}; }

        ~io_socket_idle() override = default;
    };

    /**
     * @brief io_socket template for io_socket alias producing
     * @tparam domain_v Communication domain
     * @tparam type_v Communication semantics
     * @tparam protocol_v Particular socket protol
     */
    template <int domain_v, int type_v, int protocol_v>
    struct io_socket_roasted : io_socket_idle {

        io_socket_roasted() = default;

        struct open_query : basic_query_traits<open_query> {

            typedef basic_query_traits<open_query> future_query_traits_t;
            using future_query_traits_t::_res;
            using future_query_traits_t::_waiter;

            /**
             * @param [in, out] fd reference to socket file descriptor variable
             * @param [in] flags currently unused
             */
            explicit open_query(int& fd, const int flags = 0)
                : basic_query_traits<open_query>(0) // NOTE: There is no socket but need supress on '-1' fail
                , _fd_ref(fd)
                , _flags(flags) {}

            bool query(core::kernel_waiter* kwp) const {
                core::kernel_controller::socket(kwp, domain_v, type_v, protocol_v, _flags);
                return true;
            }

            [[nodiscard]] int await_resume() const { _fd_ref = _res; return _res; }

            int& _fd_ref;
            const int _flags;
        };

        /**
         * @brief Opens socket and acquiring its file descriptor
         * @param [in] flags currently unused
         */
        auto open(const int flags = 0)
        -> open_query { return open_query {_fd, flags}; }

        ~io_socket_roasted() override = default;
    };

    /**
     * @brief io_socket class with standard 'socket(...)' constructor
     */
    struct io_socket : io_socket_idle {

        io_socket() = delete;

        explicit io_socket(const int domain, const int type, const int protocol)
            : _domain(domain)
            , _type(type)
            , _protocol(protocol) {}

        const int _domain;
        const int _type;
        const int _protocol;

        struct open_query : basic_query_traits<open_query> {

            typedef basic_query_traits future_query_traits_t;
            using future_query_traits_t::_res;
            using future_query_traits_t::_waiter;

            explicit open_query(int& fd, const int domain, const int type, const int protocol, const int flags = 0)
                : basic_query_traits(0) // NOTE: There is no socket but need supress on '-1' fail
                , _fd_ref(fd)
                , _domain(domain)
                , _type(type)
                , _protocol(protocol)
                , _flags(flags) {}

            bool query(kernel_waiter* kwp) const {
                core::kernel_controller::socket(kwp, _domain, _type, _protocol, _flags);
                return true;
            }

            [[nodiscard]] int await_resume() const { _fd_ref = _res; return _res; }

            int& _fd_ref;
            const int _domain;
            const int _type;
            const int _protocol;
            const int _flags;
        };

        /**
         * @brief Opens socket and acquiring its file descriptor
         * @param [in] flags currently unused
         */
        auto open(const int flags = 0)
        -> open_query { return open_query{_fd, _domain, _type, _protocol, flags}; }

        ~io_socket() override = default;
    };

    using io_socket_raw    = io_socket_roasted<AF_INET, SOCK_RAW, IPPROTO_RAW>;
    using io_socket_tcp    = io_socket_roasted<AF_INET, SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_tcp_v6 = io_socket_roasted<AF_INET6, SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_udp    = io_socket_roasted<AF_INET, SOCK_DGRAM, IPPROTO_UDP>;
    using io_socket_udp_v6 = io_socket_roasted<AF_INET6, SOCK_DGRAM, IPPROTO_UDP>;

} // end namespace ace::futures

#endif //ACE_NET_H
