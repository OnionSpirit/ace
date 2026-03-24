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

    template <typename entry_t>
    concept is_entry = requires(entry_t q) {
        { q._fd } -> std::same_as<int>;
        { q._is_closed } -> std::same_as<bool>;
        { q._self_sin } -> std::same_as<sockaddr_in>;
        { q._peer_sin } -> std::same_as<sockaddr_in>;
        { q.devastate() } -> std::same_as<void>;
    };

    /**
     * @brief An interface to interact with the
     * @b ace::core::kernel_controller via @b co_await operator.
     * <br>Does not define 'resume(...)' logic.
     * @tparam query_t Specific query type.
     */
    template <typename query_t>
    struct io_query_traits : future_traits<query_t>, core::kernel_waiter {

        DECLARE_FUTURE(query_t);
        IMPORT_FUTURE_ENV;

        explicit io_query_traits(const int fd) : _fd(fd) {
            static_assert(is_query<query_t>,
                "Query object shall implement 'bool query(ace::core::kernel_waiter*)' method");
        }

        struct io_socket_query_conductor : conductor_handler_t {

            io_socket_query_conductor() = delete;

            explicit io_socket_query_conductor(io_query_traits* query_)
                : _query(query_) {};

            void forward(async<>&& ctx) override {
                _query->_waiter = std::move(ctx);
            }

            void cancel() override {
                core::kernel_controller::cancel(_query, 0);
            }

            ~io_socket_query_conductor() override = default;

            io_query_traits* _query;
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

        ~io_query_traits() override = default;
    };


    /**
     * @brief Base io class with socket data and guard behavior.
     * The io_entity derived types represents socket state and provides allowed async operations
     */
    template <typename entity_t>
    struct io_entity {

        // NOTE: Don't want to use move semantics in interfaces
        mutable int  _fd;                 ///< Socket file descriptor
        mutable bool _is_closed;          ///< Socket closed flag
        mutable sockaddr_in _self_sin{};  ///< Socket self sockaddr
        mutable sockaddr_in _peer_sin{};  ///< Socket peer sockaddr

        io_entity()
            : _fd(-1)
            , _is_closed(true) {}

        io_entity(const io_entity& io) {
            _fd = io._fd;
            _is_closed = io._is_closed;
            _self_sin = io._self_sin;
            _peer_sin = io._peer_sin;
            io._fd = -1;
            io._is_closed = true;
        };

        io_entity& operator=(const io_entity& io) {
            _fd = io._fd;
            _is_closed = io._is_closed;
            _self_sin = io._self_sin;
            _peer_sin = io._peer_sin;
            io._fd = -1;
            io._is_closed = true;
            return *this;
        };

        template<typename entry_t>
        static entity_t make_from_entry(const entry_t* io) noexcept {
            entity_t entity;
            entity._fd = io->_fd;
            if (entity._fd > -1) entity._is_closed = io->_is_closed;
            else entity._is_closed = true;
            entity._self_sin = io->_self_sin;
            entity._peer_sin = io->_peer_sin;
            io->devastate();
            return entity;
        }

        // io_entity(io_entity&& io) noexcept {
        //     _fd = io._fd;
        //     _is_closed = io._is_closed;
        //     _self_sin = io._self_sin;
        //     _peer_sin = io._peer_sin;
        //     io._fd = -1;
        //     io._is_closed = true;
        // }
        //
        // io_entity& operator=(io_entity&& io)  noexcept {
        //     _fd = io._fd;
        //     _is_closed = io._is_closed;
        //     _self_sin = io._self_sin;
        //     _peer_sin = io._peer_sin;
        //     io._fd = -1;
        //     io._is_closed = true;
        //     return *this;
        // }

        struct close_query : io_query_traits<close_query> {

            using io_query_traits<close_query>::_fd;
            using io_query_traits<close_query>::_res;

            close_query() = delete;

            explicit close_query(const int fd) : io_query_traits<close_query>(fd) {}

            bool query(core::kernel_waiter* kwp) const noexcept {
                return core::kernel_controller::close(kwp, _fd);
            }

            [[nodiscard]] int await_resume() const { return _res; }
        };

        [[nodiscard]] auto close() const
            -> close_query { _is_closed = true; return close_query{_fd}; }

        virtual ~io_entity() = default;

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

            static async<> check_and_close(const bool closed, const int fd) noexcept {
                if (not closed) {
                    const int res = co_await close_query{fd};
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

    /**
     * @brief Consumable modification of the @b io_entity for the one-shot objects.
     * The @b io_entry derived types are supposed to be invalid after calling any operation of them.
     * The @b io_entity::make_from_entry(...) operation turns @b io_entry into the invalid state
     */
    template <typename entry_t>
    struct io_entry : io_entity<entry_t> {
        void devastate() const noexcept {
            io_entity<entry_t>::_is_closed = true;
            io_entity<entry_t>::_fd = -1;
        }
    };

    /**
     * @brief An @b io_entity class to represent connection socket
     * <br>Turns out from the @b io_type_entry as a result of processing its member @b connect(...)
     * or the result of @b io_listener.accept(...) via @b co_await
     */
    struct io_connection : io_entity<io_connection> {

        io_connection() = default;

        struct send_query : io_query_traits<send_query> {

            send_query() = delete;

            explicit send_query(const int fd, const void *buf, const size_t len, const int flags = 0)
                : io_query_traits(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags) {}

            bool query(kernel_waiter* kwp) const {
                return core::kernel_controller::send(kwp, _fd, _buf, _len, _flags);
            }

            [[nodiscard]] int await_resume() const { return _res; }

            const void *_buf;
            const size_t _len;
            const int _flags;
        };

        struct sendto_query : io_query_traits<sendto_query> {

            sendto_query() = delete;

            explicit sendto_query(const int fd, const void *buf, const size_t len, const int flags,
                const sockaddr *addr, const socklen_t addrlen)
                : io_query_traits(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(kernel_waiter* kwp) const {
                return core::kernel_controller::sendto(kwp, _fd, _buf, _len, _flags, _addr, _addrlen);
            }

            [[nodiscard]] int await_resume() const { return _res; }

            const void *_buf;
            const size_t _len;
            const int _flags;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        struct recv_query : io_query_traits<recv_query> {

            recv_query() = delete;

            explicit recv_query(const int fd, void *buf, const size_t len, const int flags = 0)
                : io_query_traits(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags) {}

            bool query(kernel_waiter* kwp) const {
                return core::kernel_controller::recv(kwp, _fd, _buf, _len, _flags);
            }

            [[nodiscard]] int await_resume() const { return _res; }

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

        ~io_connection() override = default;
    };

    /**
     * @brief An @b io_entity class to represent listen socket
     * <br>Turns out from the @b io_type_entry as a result of processing its member @b listen() via @b co_await
     */
    template <int domain_v = -1>
    struct io_listener : io_entity<io_listener<domain_v>> {

        typedef io_entity<io_listener> io_entity_t;
        using io_entity_t::_fd;
        using io_entity_t::_self_sin;
        using io_entity_t::_peer_sin; // NOTE: Not actual peer but stores work variable for accept query

        io_listener() = default;

        struct accept_query : io_query_traits<accept_query> {

            using io_query_traits<accept_query>::_fd;
            using io_query_traits<accept_query>::_res;

            accept_query() = delete;

            explicit accept_query(const io_listener* entry, sockaddr* addr, socklen_t* addrlen, const int flags = 0)
                : io_query_traits<accept_query>(entry->_fd)
                , _entry(entry)
                , _addr(addr)
                , _addrlen(addrlen)
                , _flags(flags) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::accept(kwp, _fd, _addr, _addrlen, _flags);
            }

            [[nodiscard]] io_connection await_resume() const {
                io_connection connection {};
                if (_res > -1) {
                    connection._fd = _res;
                    connection._is_closed = false;
                    connection._self_sin = _entry->_self_sin;
                    connection._peer_sin = *reinterpret_cast<sockaddr_in*>(_addr);
                }
                return connection;
            }

            const io_listener* _entry;
            sockaddr* _addr;
            socklen_t* _addrlen;
            const int _flags;
        };

        [[nodiscard]] auto accept(sockaddr* addr, const socklen_t* addrlen, const int flags = 0)
        -> accept_query { return accept_query{this, addr, addrlen, flags}; }

        [[nodiscard]] auto accept(const in_addr_t addr, const uint16_t port)
        -> accept_query requires (domain_v == AF_INET) {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            _peer_sin.sin_addr.s_addr = htonl(addr);
            return accept_query { this, reinterpret_cast<sockaddr*>(&_peer_sin), &peer_sin_size};
        }

        [[nodiscard]] auto accept(const std::string_view addr, const uint16_t port)
        -> accept_query requires (domain_v == AF_INET) {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_peer_sin.sin_addr));
            return accept_query { this, reinterpret_cast<sockaddr*>(&_peer_sin), peer_sin_len_ptr};
        }

        socklen_t peer_sin_size = sizeof(_peer_sin);
        socklen_t* peer_sin_len_ptr = &peer_sin_size;

        ~io_listener() override = default;
    };


    /**
     * @brief An @b io_entry class to represent socket type selection [ Listener | Connection ]
     * <br>Turns out from the @b io_bind_entry as a result of processing its member @b bind(...) via @b co_await
     */
    template <int domain_v = -1, int type_v = -1>
    struct io_type_entry : io_entry<io_type_entry<domain_v, type_v>> {

        typedef io_entry<io_type_entry> io_entry_t;
        using io_entry_t::_peer_sin;

        io_type_entry() : io_entry_t() {};

        struct listen_query : io_query_traits<listen_query> {

            using io_query_traits<listen_query>::_fd;

            listen_query() = delete;

            explicit listen_query(io_type_entry&& entry, const int backlog)
                : io_query_traits<listen_query>(entry._fd)
                , _entry(entry)
                , _backlog(backlog) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::listen(kwp, _fd, _backlog);
            }

            [[nodiscard]] io_listener<domain_v> await_resume() const {
                return io_listener<domain_v>::make_from_entry(&_entry);
            }

            io_type_entry& _entry;
            const int _backlog;
        };

        struct connect_query : io_query_traits<connect_query> {

            using io_query_traits<connect_query>::_fd;

            connect_query() = delete;

            explicit connect_query(io_type_entry&& entry, const sockaddr* addr, const socklen_t addrlen)
                : io_query_traits<connect_query>(entry._fd)
                , _entry(entry)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::connect(kwp, _fd, _addr, _addrlen);
            }

            [[nodiscard]] io_connection await_resume() const {
                return io_connection::make_from_entry(&_entry);
            }

            io_type_entry& _entry;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        [[nodiscard]] auto listen(const int backlog = 0)
        -> listen_query requires (type_v == SOCK_SEQPACKET or type_v == SOCK_STREAM) {
            return listen_query{ std::move(*this), backlog};
        }

        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen)
        -> connect_query { return connect_query{ std::move(*this), addr, addrlen}; }

        [[nodiscard]] auto connect(const in_addr_t addr, const uint16_t port)
        -> connect_query requires (domain_v == AF_INET) {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            _peer_sin.sin_addr.s_addr = htonl(addr);
            return connect_query { std::move(*this), reinterpret_cast<sockaddr*>(&_peer_sin), sizeof(_peer_sin)};
        }

        [[nodiscard]] auto connect(const std::string_view addr, const uint16_t port)
        -> connect_query requires (domain_v == AF_INET) {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_peer_sin.sin_addr));
            return connect_query { std::move(*this), reinterpret_cast<sockaddr*>(&_peer_sin), sizeof(_peer_sin)};
        }

        ~io_type_entry() override = default;
    };


    /**
     * @brief An @b io_entry class to represent waiting for binding
     * <br>Turns out from @b io_socket_entry as a result of processing it via @b co_await
     */
    template <int domain_v = -1, int type_v = -1>
    struct io_bind_entry : io_entry<io_bind_entry<domain_v, type_v>> {

        typedef io_entry<io_bind_entry> io_entry_t;
        using io_entry_t::_self_sin;

        io_bind_entry() : io_entry_t() {};

        explicit io_bind_entry(const int fd) {
            io_entry_t::_fd = fd;
            if (io_entry_t::_fd > -1) io_entry_t::_is_closed = false;
        }

        struct bind_query : io_query_traits<bind_query> {

            using io_query_traits<bind_query>::_fd;

            bind_query() = delete;

            explicit bind_query(io_bind_entry&& entry, const sockaddr* addr, const socklen_t addrlen)
                : io_query_traits<bind_query>(entry._fd)
                , _entry(entry)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::bind(kwp, _fd, _addr, _addrlen);
            }

            [[nodiscard]] io_type_entry<domain_v, type_v> await_resume() {
                return io_type_entry<domain_v, type_v>::make_from_entry(&_entry);
            }

            io_bind_entry& _entry;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        [[nodiscard]] auto bind(const sockaddr* addr, const socklen_t addrlen)
        -> bind_query { return bind_query { std::move(*this), addr, addrlen}; }

        [[nodiscard]] auto bind(const in_addr_t addr, const uint16_t port)
        -> bind_query requires (domain_v == AF_INET) {
            _self_sin.sin_family = domain_v;
            _self_sin.sin_port = htons(port);
            _self_sin.sin_addr.s_addr = htonl(addr);
            return bind_query { std::move(*this), reinterpret_cast<sockaddr*>(&_self_sin), sizeof(_self_sin)};
        }

        [[nodiscard]] auto bind(const std::string_view addr, const uint16_t port)
        -> bind_query requires (domain_v == AF_INET) {
            _self_sin.sin_family = domain_v;
            _self_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_self_sin.sin_addr));
            return bind_query { std::move(*this), reinterpret_cast<sockaddr*>(&_self_sin), sizeof(_self_sin)};
        }

        ~io_bind_entry() override = default;
    };


    /**
     * @brief An @b io_entry for socket creation. Also, supports aliasing
     * @tparam domain_v Communication domain
     * @tparam type_v Communication semantics
     * @tparam protocol_v Particular socket protol
     */
    template <int domain_v = -1, int type_v = -1, int protocol_v = -1>
    struct io_socket_entry : io_query_traits<io_socket_entry<domain_v, type_v, protocol_v>> {

        typedef io_query_traits<io_socket_entry> io_query_traits_t;
        using io_query_traits_t::_res;

        /**
         * @param [in] flags currently unused
         */
        explicit io_socket_entry(const int flags = 0)
            // NOTE: There is no socket but need supress defaulted '-1' errcode
            : io_query_traits<io_socket_entry>(0)
            , _flags(flags) {}

        bool query(core::kernel_waiter* kwp) const {
            core::kernel_controller::socket(kwp, domain_v, type_v, protocol_v, _flags);
            return true;
        }

        [[nodiscard]] io_bind_entry<domain_v, type_v> await_resume() const {
            return io_bind_entry<domain_v, type_v>{_res};
        }

        const int _flags;
    };


    template <>
    struct io_socket_entry<-1, -1, -1> : io_query_traits<io_socket_entry<>> {

        typedef io_query_traits io_query_traits_t;
        using io_query_traits_t::_res;

        /**
         * @param [in] domain communication domain
         * @param [in] type communication semantics
         * @param [in] protocol particular socket protol
         * @param [in] flags currently unused
         */
        explicit io_socket_entry(const int domain, const int type, const int protocol, const int flags = 0)
            // NOTE: There is no socket but need supress defaulted '-1' errcode
            : io_query_traits(0)
            , _domain(domain)
            , _type(type)
            , _protocol(protocol)
            , _flags(flags) {}

        bool query(core::kernel_waiter* kwp) const {
            core::kernel_controller::socket(kwp, _domain, _type, _protocol, _flags);
            return true;
        }

        [[nodiscard]] io_bind_entry<> await_resume() const { return io_bind_entry{_res}; }

        const int _domain;
        const int _type;
        const int _protocol;
        const int _flags;
    };

    using io_socket_raw_entry      = io_socket_entry<AF_INET , SOCK_RAW   , IPPROTO_RAW>;
    using io_socket_raw_dual_entry = io_socket_entry<AF_INET6, SOCK_RAW   , IPPROTO_RAW>;
    using io_socket_tcp_entry      = io_socket_entry<AF_INET , SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_tcp_dual_entry = io_socket_entry<AF_INET6, SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_udp_entry      = io_socket_entry<AF_INET , SOCK_DGRAM , IPPROTO_UDP>;
    using io_socket_udp_dual_entry = io_socket_entry<AF_INET6, SOCK_DGRAM , IPPROTO_UDP>;

} // end namespace ace::futures

#endif //ACE_NET_H
