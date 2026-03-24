#ifndef ACE_NET_H
#define ACE_NET_H

#include <arpa/inet.h>
#include <netinet/in.h>

#include "ace/core/io.h"

namespace ace::futures {

    // TODO: Make more common options for io_entity and io_query

    template <typename entry_t>
    concept is_entry = requires(entry_t q) {
        { q._fd } -> std::same_as<int>;
        { q._is_closed } -> std::same_as<bool>;
        { q._self_sin } -> std::same_as<sockaddr_in>;
        { q._peer_sin } -> std::same_as<sockaddr_in>;
        { q.clear() } -> std::same_as<void>;
    };


    /**
     * @brief Base io class with socket data and guard behavior.
     * The io_entity derived types represents socket state and provides allowed async operations
     */
    template <typename entity_t>
    struct io_entity {

        int  _fd;                 ///< Socket file descriptor
        bool _is_closed;          ///< Socket closed flag
        sockaddr_in _self_sin{};  ///< Socket self sockaddr
        sockaddr_in _peer_sin{};  ///< Socket peer sockaddr

        io_entity()
            : _fd(-1)
            , _is_closed(true) {}

        io_entity(const int fd, const bool is_closed, const sockaddr_in self, const sockaddr_in peer)
            : _fd(fd)
            , _is_closed(is_closed)
            , _self_sin(self)
            , _peer_sin(peer) { };

        template<typename entry_t>
        static entity_t make_from_entry(entry_t* io) noexcept {
            int fd = io->_fd;
            bool is_closed;
            if (fd > -1) is_closed = io->_is_closed;
            else is_closed = true;
            sockaddr_in self = io->_self_sin;
            sockaddr_in peer = io->_peer_sin;
            io->clear();
            return entity_t {fd, is_closed, self, peer};
        }

        io_entity(io_entity&& io) noexcept {
            _fd = io._fd;
            _is_closed = io._is_closed;
            _self_sin = io._self_sin;
            _peer_sin = io._peer_sin;
            io._fd = -1;
            io._is_closed = true;
        }

        io_entity& operator=(io_entity&& io)  noexcept {
            _fd = io._fd;
            _is_closed = io._is_closed;
            _self_sin = io._self_sin;
            _peer_sin = io._peer_sin;
            io._fd = -1;
            io._is_closed = true;
            return *this;
        }

        [[nodiscard]] auto close()
            -> core::close_query { _is_closed = true; return core::close_query{_fd}; }

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


#define IMPORT_ERROR_HANDLING                                                                 \
    operator bool() const { return _fd > -1 or INT_MIN == _fd; }                              \
    std::string_view error() const {                                                          \
        if (_fd > -1)                                                                         \
            throw std::logic_error("can not receive 'error()' on successed 'io_entity'");     \
        if (INT_MIN == _fd)                                                                   \
            throw std::logic_error("can not receive 'error()' on idle 'io_entry'");           \
        return strerror(_fd);                                                                 \
    }


#define IMPORT_IO_ENTITY_ENV(class) typedef io_entity<class> io_entity_t;                     \
    class(const int fd, const bool is_closed, const sockaddr_in self, const sockaddr_in peer) \
        : io_entity_t(fd, is_closed, self, peer) { };                                         \
    using io_entity_t::_fd;                                                                   \
    using io_entity_t::_self_sin;                                                             \
    using io_entity_t::_peer_sin;                                                             \
    using io_entity_t::_is_closed;                                                            \
    IMPORT_ERROR_HANDLING



    // TODO: Use memmove instead of params on commonized io_entry
    /**
     * @brief Consumable modification of the @b io_entity for the one-shot objects.
     * The @b io_entry derived types are supposed to be invalid after calling any operation of them.
     * The @b io_entity::make_from_entry(...) operation turns @b io_entry into the invalid state
     */
    template <typename entry_t>
    struct io_entry : io_entity<entry_t> {

        io_entry() = default;

        io_entry(const int fd, const bool is_closed, const sockaddr_in self, const sockaddr_in peer)
            : io_entity<entry_t>(fd, is_closed, self, peer) { };

        using io_entity<entry_t>::_fd;
        using io_entity<entry_t>::_is_closed;

        void clear() noexcept {
            _is_closed = true;
            _fd = -1;
        }
    };

    // TODO: Use memmove instead of params on commonized io_entry
    // TODO: Try to make it mixin
    #define IMPORT_IO_ENTRY_ENV(class) typedef io_entry<class> io_entry_t;                       \
    class(const int fd, const bool is_closed, const sockaddr_in self, const sockaddr_in peer)    \
        : io_entry_t(fd, is_closed, self, peer) { };                                             \
    using io_entry_t::_fd;                                                                       \
    using io_entry_t::_self_sin;                                                                 \
    using io_entry_t::_peer_sin;                                                                 \
    using io_entry_t::_is_closed;                                                                \
    IMPORT_ERROR_HANDLING


    /**
     * @brief An @b io_entity class to represent connection socket
     * <br>Turns out from the @b io_selection_entry as a result of processing its member @b connect(...)
     * or the result of @b io_listener.accept(...) via @b co_await
     */
    struct io_connection : io_entity<io_connection> {

        IMPORT_IO_ENTITY_ENV(io_connection)

        io_connection() = default;

        struct send_query : core::io_query<send_query> {

            send_query() = delete;

            explicit send_query(const int fd, const void *buf, const size_t len, const int flags = 0)
                : io_query(fd)
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

        struct sendto_query : core::io_query<sendto_query> {

            sendto_query() = delete;

            explicit sendto_query(const int fd, const void *buf, const size_t len, const int flags,
                const sockaddr *addr, const socklen_t addrlen)
                : io_query(fd)
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

        struct recv_query : core::io_query<recv_query> {

            recv_query() = delete;

            explicit recv_query(const int fd, void *buf, const size_t len, const int flags = 0)
                : io_query(fd)
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
     * <br>Turns out from the @b io_selection_entry as a result of processing its member @b listen()
     * via @b co_await
     */
    template <int domain_v = -1>
    struct io_listener : io_entity<io_listener<domain_v>> {

        IMPORT_IO_ENTITY_ENV(io_listener);

        io_listener() = default;

        struct accept_query : core::io_query<accept_query> {

            IMPORT_IO_QUERY_ENV(accept_query)

            accept_query() = delete;

            explicit accept_query(const io_listener* entry, sockaddr* addr, socklen_t* addrlen, const int flags = 0)
                : io_query_t(entry->_fd)
                , _entry(entry)
                , _addr(addr)
                , _addrlen(addrlen)
                , _flags(flags) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::accept(kwp, _fd, _addr, _addrlen, _flags);
            }

            [[nodiscard]] io_connection await_resume() const {
                if (_res > -1)
                    return io_connection { _res, false,
                        _entry->_self_sin, *reinterpret_cast<sockaddr_in*>(_addr) };
                return io_connection {};
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
    struct io_selection_entry : io_entry<io_selection_entry<domain_v, type_v>> {

        IMPORT_IO_ENTRY_ENV(io_selection_entry)

        io_selection_entry() : io_entry_t() {};

        struct listen_query : core::io_query<listen_query> {

            IMPORT_IO_QUERY_ENV(listen_query)

            listen_query() = delete;

            explicit listen_query(io_selection_entry&& entry, const int backlog)
                : io_query_t(entry._fd)
                , _entry(entry)
                , _backlog(backlog) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::listen(kwp, _fd, _backlog);
            }

            [[nodiscard]] io_listener<domain_v> await_resume() const {
                return io_listener<domain_v>::make_from_entry(&_entry);
            }

            io_selection_entry& _entry;
            const int _backlog;
        };

        struct connect_query : core::io_query<connect_query> {

            IMPORT_IO_QUERY_ENV(connect_query)

            connect_query() = delete;

            explicit connect_query(io_selection_entry&& entry, const sockaddr* addr, const socklen_t addrlen)
                : io_query_t(entry._fd)
                , _entry(entry)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::connect(kwp, _fd, _addr, _addrlen);
            }

            [[nodiscard]] io_connection await_resume() const {
                return io_connection::make_from_entry(&_entry);
            }

            io_selection_entry& _entry;
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

        ~io_selection_entry() override = default;
    };


    /**
     * @brief An @b io_entry class to represent waiting for binding
     * <br>Turns out from @b io_socket_entry as a result of processing it via @b co_await
     */
    template <int domain_v = -1, int type_v = -1>
    struct io_bind_entry : io_entry<io_bind_entry<domain_v, type_v>> {

        IMPORT_IO_ENTRY_ENV(io_bind_entry)

        io_bind_entry() : io_entry_t() {};

        explicit io_bind_entry(const int fd) {
            io_entry_t::_fd = fd;
            if (io_entry_t::_fd > -1) io_entry_t::_is_closed = false;
        }

        struct bind_query : core::io_query<bind_query> {

            IMPORT_IO_QUERY_ENV(bind_query)

            bind_query() = delete;

            explicit bind_query(io_bind_entry&& entry, const sockaddr* addr, const socklen_t addrlen)
                : io_query_t(entry._fd)
                , _entry(entry)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::bind(kwp, _fd, _addr, _addrlen);
            }

            [[nodiscard]] io_selection_entry<domain_v, type_v> await_resume() {
                return io_selection_entry<domain_v, type_v>::make_from_entry(&_entry);
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
    struct io_socket : core::io_query<io_socket<domain_v, type_v, protocol_v>> {

        IMPORT_IO_QUERY_ENV(io_socket)

        /**
         * @param [in] flags currently unused
         */
        explicit io_socket(const int flags = 0)
            // NOTE: There is no socket but need supress defaulted '-1' errcode
            : io_query_t(0)
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
    struct io_socket<-1, -1, -1> : core::io_query<io_socket<>> {

        IMPORT_IO_QUERY_ENV(io_socket)

        /**
         * @param [in] domain communication domain
         * @param [in] type communication semantics
         * @param [in] protocol particular socket protol
         * @param [in] flags currently unused
         */
        explicit io_socket(const int domain, const int type, const int protocol, const int flags = 0)
            // NOTE: There is no socket but need supress defaulted '-1' errcode
            : io_query_t(0)
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

    using io_socket_raw      = io_socket<AF_INET , SOCK_RAW   , IPPROTO_RAW>;
    using io_socket_raw_dual = io_socket<AF_INET6, SOCK_RAW   , IPPROTO_RAW>;
    using io_socket_tcp      = io_socket<AF_INET , SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_tcp_dual = io_socket<AF_INET6, SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_udp      = io_socket<AF_INET , SOCK_DGRAM , IPPROTO_UDP>;
    using io_socket_udp_dual = io_socket<AF_INET6, SOCK_DGRAM , IPPROTO_UDP>;

} // end namespace ace::futures

#endif //ACE_NET_H
