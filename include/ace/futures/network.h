#ifndef ACE_NET_H
#define ACE_NET_H

#include <arpa/inet.h>
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
        mutable int  _fd;                 ///< Socket file descriptor
        mutable bool _is_closed;          ///< Socket closed flag
        mutable sockaddr_in _self_sin{};  ///< Socket self sockaddr
        mutable sockaddr_in _peer_sin{};  ///< Socket peer sockaddr

        io_socket_base()
            : _fd(-1)
            , _is_closed(true) {}

        io_socket_base(const io_socket_base&) = delete;

        io_socket_base& operator=(const io_socket_base&) = delete;

        io_socket_base(io_socket_base&& io) noexcept {
            _fd = io._fd;
            _is_closed = io._is_closed;
            _self_sin = io._self_sin;
            _peer_sin = io._peer_sin;
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

        void remove_ownership() const noexcept {
            _is_closed = true;
            _fd = -1;
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

        [[nodiscard]] auto close() const
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
                    if (res < 0) std::cerr << strerror(res) << std::endl;
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

        explicit io_socket_connection(const io_socket_base* io) noexcept {
            _fd = io->_fd;
            if (_fd > -1) _is_closed = io->_is_closed;
            else _is_closed = true;
            _self_sin = io->_self_sin;
            _peer_sin = io->_peer_sin;
            io->remove_ownership();
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
    template <int domain_v = -1>
    struct io_socket_listener : io_socket_base  {

        io_socket_listener() = delete;

        explicit io_socket_listener(const int fd) {
            _fd = fd;
            if (_fd > -1) _is_closed = false;
        }

        explicit io_socket_listener(const io_socket_base* io) noexcept {
            _fd = io->_fd;
            if (_fd > -1) _is_closed = io->_is_closed;
            else _is_closed = true;
            _self_sin = io->_self_sin;
            io->remove_ownership();
        }

        struct accept_query : basic_query_traits<accept_query> {

            using basic_query_traits<accept_query>::_fd;
            using basic_query_traits<accept_query>::_res;

            accept_query() = delete;

            explicit accept_query(const io_socket_listener* sock, sockaddr* addr, socklen_t* addrlen, const int flags = 0)
                : basic_query_traits<accept_query>(sock->_fd)
                , _sock(sock)
                , _addr(addr)
                , _addrlen(addrlen)
                , _flags(flags) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::accept(kwp, _fd, _addr, _addrlen, _flags);
            }

            [[nodiscard]] io_socket_connection await_resume() const {
                const io_socket_base io_sock {};
                if (_res > -1) {
                    io_sock._fd = _res;
                    io_sock._is_closed = false;
                    io_sock._self_sin = _sock->_self_sin;
                    io_sock._peer_sin = *reinterpret_cast<sockaddr_in*>(_addr);
                }
                return io_socket_connection{&io_sock};
            }

            const io_socket_listener* _sock;
            sockaddr* _addr;
            socklen_t* _addrlen;
            const int _flags;
        };

        [[nodiscard]] auto accept(sockaddr* addr, socklen_t* addrlen, const int flags = 0) const
        -> accept_query { return accept_query{this, addr, addrlen, flags}; }

        [[nodiscard]] auto accept(const in_addr_t addr, const uint16_t port) const
        -> accept_query requires (domain_v == AF_INET) {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            _peer_sin.sin_addr.s_addr = htonl(addr);
            return accept_query {this, reinterpret_cast<sockaddr*>(&_peer_sin), &peer_sin_size};
        }

        [[nodiscard]] auto accept(const std::string_view addr, const uint16_t port) const
        -> accept_query requires (domain_v == AF_INET) {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_peer_sin.sin_addr));
            return accept_query {this, reinterpret_cast<sockaddr*>(&_peer_sin), peer_sin_len_ptr};
        }

        socklen_t peer_sin_size = sizeof(_peer_sin);
        socklen_t* peer_sin_len_ptr = &peer_sin_size;

        ~io_socket_listener() override = default;
    };


    /**
     * @brief io_socket class that represents bint socket on initial state
     */
    template <int domain_v = -1, int type_v = -1>
    struct io_socket_bint : io_socket_base {

        io_socket_bint() : io_socket_base() {};

        explicit io_socket_bint(const int fd) {
            _fd = fd;
            if (_fd > -1) _is_closed = false;
        }

        explicit io_socket_bint(const io_socket_base* io) noexcept {
            _fd = io->_fd;
            if (_fd > -1) _is_closed = io->_is_closed;
            else _is_closed = true;
            _self_sin = io->_self_sin;
            io->remove_ownership();
        }

        struct listen_query : basic_query_traits<listen_query> {

            using basic_query_traits<listen_query>::_fd;

            listen_query() = delete;

            explicit listen_query(const io_socket_bint* sock, const int backlog)
                : basic_query_traits<listen_query>(sock->_fd)
                , _sock(sock)
                , _backlog(backlog) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::listen(kwp, _fd, _backlog);
            }

            [[nodiscard]] io_socket_listener<domain_v> await_resume() const {
                return io_socket_listener<domain_v>{_sock};
            }

            const io_socket_bint* _sock;
            const int _backlog;
        };

        struct connect_query : basic_query_traits<connect_query> {

            using basic_query_traits<connect_query>::_fd;

            connect_query() = delete;

            explicit connect_query(const io_socket_bint* sock, const sockaddr* addr, const socklen_t addrlen)
                : basic_query_traits<connect_query>(sock->_fd)
                , _sock(sock)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::connect(kwp, _fd, _addr, _addrlen);
            }

            [[nodiscard]] io_socket_connection await_resume() const {
                return io_socket_connection{_sock};
            }

            const io_socket_bint* _sock;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        [[nodiscard]] auto listen(const int backlog = 0) const
        -> listen_query requires (type_v == SOCK_SEQPACKET or type_v == SOCK_STREAM) {
            return listen_query{this, backlog};
        }

        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen) const
        -> connect_query { return connect_query{this, addr, addrlen}; }

        [[nodiscard]] auto connect(const in_addr_t addr, const uint16_t port) const
        -> connect_query requires (domain_v == AF_INET) {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            _peer_sin.sin_addr.s_addr = htonl(addr);
            return connect_query {this, reinterpret_cast<sockaddr*>(&_peer_sin), sizeof(_peer_sin)};
        }

        [[nodiscard]] auto connect(const std::string_view addr, const uint16_t port) const
        -> connect_query requires (domain_v == AF_INET) {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_peer_sin.sin_addr));
            return connect_query {this, reinterpret_cast<sockaddr*>(&_peer_sin), sizeof(_peer_sin)};
        }

        ~io_socket_bint() override = default;
    };


    /**
     * @brief io_socket class that represents idle socket that waiting for binding
     */
    template <int domain_v = -1, int type_v = -1>
    struct io_socket_idle : io_socket_base {

        io_socket_idle() : io_socket_base() {};

        explicit io_socket_idle(const int fd) {
            _fd = fd;
            if (_fd > -1) _is_closed = false;
        }

        struct bind_query : basic_query_traits<bind_query> {

            using basic_query_traits<bind_query>::_fd;

            bind_query() = delete;

            explicit bind_query(const io_socket_idle* sock, const sockaddr* addr, const socklen_t addrlen)
                : basic_query_traits<bind_query>(sock->_fd)
                , _sock(sock)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::bind(kwp, _fd, _addr, _addrlen);
            }

            [[nodiscard]] io_socket_bint<domain_v, type_v> await_resume() {
                return io_socket_bint<domain_v, type_v>{_sock};
            }

            const io_socket_idle* _sock;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        [[nodiscard]] auto bind(const sockaddr* addr, const socklen_t addrlen) const
        -> bind_query { return bind_query {this, addr, addrlen}; }

        [[nodiscard]] auto bind(const in_addr_t addr, const uint16_t port)
        -> bind_query requires (domain_v == AF_INET) {
            _self_sin.sin_family = domain_v;
            _self_sin.sin_port = htons(port);
            _self_sin.sin_addr.s_addr = htonl(addr);
            return bind_query {this, reinterpret_cast<sockaddr*>(&_self_sin), sizeof(_self_sin)};
        }

        [[nodiscard]] auto bind(const std::string_view addr, const uint16_t port)
        -> bind_query requires (domain_v == AF_INET) {
            _self_sin.sin_family = domain_v;
            _self_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_self_sin.sin_addr));
            return bind_query {this, reinterpret_cast<sockaddr*>(&_self_sin), sizeof(_self_sin)};
        }

        ~io_socket_idle() override = default;
    };


    /**
     * @brief io_socket future for socket creation. Also, supports aliasing
     * @tparam domain_v Communication domain
     * @tparam type_v Communication semantics
     * @tparam protocol_v Particular socket protol
     */
    template <int domain_v = -1, int type_v = -1, int protocol_v = -1>
    struct io_socket : io_socket_base::basic_query_traits<io_socket<domain_v, type_v, protocol_v>> {

        typedef io_socket_base::basic_query_traits<io_socket> future_query_traits_t;
        using future_query_traits_t::_res;
        using future_query_traits_t::_waiter;

        /**
         * @param [in] flags currently unused
         */
        explicit io_socket(const int flags = 0)
            // NOTE: There is no socket but need supress defaulted '-1' errcode
            : io_socket_base::basic_query_traits<io_socket>(0)
            , _flags(flags) {}

        bool query(core::kernel_waiter* kwp) const {
            core::kernel_controller::socket(kwp, domain_v, type_v, protocol_v, _flags);
            return true;
        }

        [[nodiscard]] io_socket_idle<domain_v, type_v> await_resume() const {
            return io_socket_idle<domain_v, type_v>{_res};
        }

        const int _flags;
    };


    template <>
    struct io_socket<-1, -1, -1> : io_socket_base::basic_query_traits<io_socket<>> {

        typedef basic_query_traits future_query_traits_t;
        using future_query_traits_t::_res;
        using future_query_traits_t::_waiter;

        /**
         * @param [in] domain communication domain
         * @param [in] type communication semantics
         * @param [in] protocol particular socket protol
         * @param [in] flags currently unused
         */
        explicit io_socket(const int domain, const int type, const int protocol, const int flags = 0)
            // NOTE: There is no socket but need supress defaulted '-1' errcode
            : basic_query_traits<io_socket>(0)
            , _domain(domain)
            , _type(type)
            , _protocol(protocol)
            , _flags(flags) {}

        bool query(core::kernel_waiter* kwp) const {
            core::kernel_controller::socket(kwp, _domain, _type, _protocol, _flags);
            return true;
        }

        [[nodiscard]] io_socket_idle<> await_resume() const { return io_socket_idle{_res}; }

        const int _domain;
        const int _type;
        const int _protocol;
        const int _flags;
    };

    using io_socket_raw      = io_socket<AF_INET , SOCK_RAW   , IPPROTO_RAW>;
    using io_socket_raw_dual = io_socket<AF_INET6, SOCK_RAW   , IPPROTO_RAW>;
    using io_socket_tcp      = io_socket<AF_INET , SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_tcp_dual = io_socket<AF_INET6, SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_udp      = io_socket<AF_INET , SOCK_DGRAM , IPPROTO_UDP>;
    using io_socket_udp_dual = io_socket<AF_INET6, SOCK_DGRAM , IPPROTO_UDP>;

} // end namespace ace::futures

#endif //ACE_NET_H
