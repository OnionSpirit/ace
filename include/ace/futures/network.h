#ifndef ACE_NET_H
#define ACE_NET_H

#include <arpa/inet.h>
#include <netinet/in.h>

#include "ace/core/io.h"

namespace ace::futures {

    template <typename entity_t>
    using io_net_entity = core::io_entity<entity_t, sockaddr_in, sockaddr_in>;

    #define IMPORT_IO_NET_ENTITY_ENV(class) IMPORT_IO_ENTITY_ENV(class, sockaddr_in, sockaddr_in);

    #define SELF_SIN std::get<0>(_params)
    #define PEER_SIN std::get<1>(_params)

    inline auto& self_sin_from(std::tuple<sockaddr_in, sockaddr_in> p) { return std::get<0>(p); }
    inline auto& peer_sin_from(std::tuple<sockaddr_in, sockaddr_in> p) { return std::get<1>(p); }

    template <int domain_v>
    inline constexpr bool is_inet_domain = domain_v == AF_INET or domain_v == AF_INET6;

    template <int type_v>
    inline constexpr bool is_stream_type = type_v == SOCK_STREAM;

    template <typename, int>
    struct connect_query;

    /**
     * @brief An @b io_entity class to represent connection socket
     * <br>Turns out from the @b io_selection_entry as a result of processing its member @b connect(...)
     * or the result of @b io_listener.accept(...) via @b co_await
     */
    template <int domain_v = -1, bool is_connected_v = false>
    struct io_transport_entity : io_net_entity<io_transport_entity<domain_v, is_connected_v>> {

        IMPORT_IO_NET_ENTITY_ENV(io_transport_entity)

        io_transport_entity() = default;

        using connect_query_t = connect_query<io_transport_entity, domain_v>;

        struct send_query : core::io_query<send_query> {

            IMPORT_IO_QUERY_ENV(send_query);

            send_query() = delete;

            explicit send_query(const int fd, const void *buf, const size_t len, const int flags = 0)
                : io_query_t(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags) {}

            bool setup_query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::send(kwp, _fd, _buf, _len, _flags);
            }

            [[nodiscard]] int await_resume() const { return _res; }

            const void *_buf;
            const size_t _len;
            const int _flags;
        };

        struct sendto_query : core::io_query<sendto_query> {

            IMPORT_IO_QUERY_ENV(sendto_query);

            sendto_query() = delete;

            explicit sendto_query(const int fd, const void *buf, const size_t len, const int flags,
                const sockaddr *addr, const socklen_t addrlen)
                : io_query_t(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool setup_query(core::kernel_waiter* kwp) const {
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

            IMPORT_IO_QUERY_ENV(recv_query)

            recv_query() = delete;

            explicit recv_query(const int fd, void *buf, const size_t len, const int flags = 0)
                : io_query_t(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags) {}

            bool setup_query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::recv(kwp, _fd, _buf, _len, _flags);
            }

            [[nodiscard]] int await_resume() const { return _res; }

            void *_buf;
            const size_t _len;
            const int _flags;
        };

        [[nodiscard]] auto send(const void *buf, const size_t len, const int flags = 0) const
        -> send_query requires is_connected_v { return send_query{_fd, buf, len, flags}; }

        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen)
        -> connect_query_t requires (not is_connected_v) { return connect_query_t{ std::move(*this), addr, addrlen}; }

        [[nodiscard]] auto connect(const in_addr_t addr, const uint16_t port)
        -> connect_query_t requires (is_inet_domain<domain_v> and not is_connected_v) {
            PEER_SIN.sin_family = domain_v;
            PEER_SIN.sin_port = htons(port);
            PEER_SIN.sin_addr.s_addr = htonl(addr);
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&PEER_SIN), sizeof(PEER_SIN)};
        }

        [[nodiscard]] auto connect(const std::string_view addr, const uint16_t port)
        -> connect_query_t requires (is_inet_domain<domain_v> and not is_connected_v) {
            PEER_SIN.sin_family = domain_v;
            PEER_SIN.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(PEER_SIN.sin_addr));
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&PEER_SIN), sizeof(PEER_SIN)};
        }

        [[nodiscard]] auto sendto(const void *buf, const size_t len, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query { return sendto_query{_fd, buf, len, flags, addr, addrlen}; }

        [[nodiscard]] auto recv(void *buf, const size_t len, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf, len, flags}; }

    };

    template <typename entry_t, int domain_v = -1>
    struct connect_query : core::io_query<connect_query<entry_t, domain_v>> {

        IMPORT_IO_QUERY_ENV(connect_query)

        connect_query() = delete;

        typedef io_transport_entity<domain_v, true> io_transport_entity_t;

        explicit connect_query(entry_t&& entry, const sockaddr* addr, const socklen_t addrlen)
            : io_query_t(entry._fd)
            , _entry(entry)
            , _addr(addr)
            , _addrlen(addrlen) {}

        bool setup_query(core::kernel_waiter* kwp) const {
            return core::kernel_controller::connect(kwp, _fd, _addr, _addrlen);
        }

        [[nodiscard]] io_transport_entity_t await_resume() const {
            if (_res > -1) {
                return io_transport_entity_t::make_from_entry(&_entry);
            }
            return io_transport_entity_t {};
        }

        entry_t& _entry;
        const sockaddr* _addr;
        const socklen_t _addrlen;
    };

    /**
     * @brief An @c io_entity class to represent listen socket
     *
     * Turns out from the @c io_stream_mode_entry as a result of processing its member @c listen()
     * via @c co_await
     */
    template <int domain_v = -1>
    struct io_listener_entity : io_net_entity<io_listener_entity<domain_v>> {

        IMPORT_IO_NET_ENTITY_ENV(io_listener_entity);

        io_listener_entity() = default;

        struct accept_query : core::io_query<accept_query> {

            IMPORT_IO_QUERY_ENV(accept_query)

            accept_query() = delete;

            typedef io_transport_entity<domain_v, true> io_transport_entity_t;

            explicit accept_query(const io_listener_entity* entry, sockaddr* addr, socklen_t* addrlen, const int flags = 0)
                : io_query_t(entry->_fd)
                , _entry(entry)
                , _addr(addr)
                , _addrlen(addrlen)
                , _flags(flags) {}

            bool setup_query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::accept(kwp, _fd, _addr, _addrlen, _flags);
            }

            [[nodiscard]] io_transport_entity_t await_resume() const {
                if (_res > -1) {
                    peer_sin_from(_entry->_params) = *reinterpret_cast<sockaddr_in*>(_addr);
                    return io_transport_entity_t { _res, false, _entry->_params };
                }
                return io_transport_entity_t {};
            }

            const io_listener_entity* _entry;
            sockaddr* _addr;
            socklen_t* _addrlen;
            const int _flags;
        };

        [[nodiscard]] auto accept()
        -> accept_query { return accept_query { this, reinterpret_cast<sockaddr*>(&SELF_SIN), &self_sin_size}; }

        [[nodiscard]] auto accept(sockaddr* addr, const socklen_t* addrlen, const int flags = 0)
        -> accept_query { return accept_query{this, addr, addrlen, flags}; }

        [[nodiscard]] auto accept(const in_addr_t addr, const uint16_t port)
        -> accept_query requires is_inet_domain<domain_v> {
            SELF_SIN.sin_family = domain_v;
            SELF_SIN.sin_port = htons(port);
            SELF_SIN.sin_addr.s_addr = htonl(addr);
            return accept_query { this, reinterpret_cast<sockaddr*>(&SELF_SIN), &self_sin_size};
        }

        [[nodiscard]] auto accept(const std::string_view addr, const uint16_t port)
        -> accept_query requires is_inet_domain<domain_v> {
            SELF_SIN.sin_family = domain_v;
            SELF_SIN.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(SELF_SIN.sin_addr));
            return accept_query { this, reinterpret_cast<sockaddr*>(&SELF_SIN), self_sin_len_ptr};
        }

        socklen_t self_sin_size = sizeof(SELF_SIN);
        socklen_t* self_sin_len_ptr = &self_sin_size;

    };


    /**
     * @brief An @c io_entry class to represent socket mode selection [ @b Listener | @b Connection ]
     *
     * Turns out from the @c io_mapping_entry only for the @b SOCK_STREAM socket type
     * as a result of processing its member @c bind(...) via @c co_await
     */
    template <int domain_v = -1, int type_v = -1>
    struct io_stream_mode_entry
        : io_net_entity<io_stream_mode_entry<domain_v, type_v>>
        , core::io_entry<io_stream_mode_entry<domain_v, type_v>> {

        IMPORT_IO_NET_ENTITY_ENV(io_stream_mode_entry)

        io_stream_mode_entry() : io_entity_t() {};

        struct listen_query : core::io_query<listen_query> {

            IMPORT_IO_QUERY_ENV(listen_query)

            listen_query() = delete;

            explicit listen_query(io_stream_mode_entry&& entry, const int backlog)
                : io_query_t(entry._fd)
                , _entry(entry)
                , _backlog(backlog) {}

            bool setup_query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::listen(kwp, _fd, _backlog);
            }

            [[nodiscard]] io_listener_entity<domain_v> await_resume() const {
                return io_listener_entity<domain_v>::make_from_entry(&_entry);
            }

            io_stream_mode_entry& _entry;
            const int _backlog;
        };


        [[nodiscard]] auto listen(const int backlog = 0)
        -> listen_query requires (type_v == SOCK_SEQPACKET or type_v == SOCK_STREAM) {
            return listen_query{ std::move(*this), backlog};
        }

        using connect_query_t = connect_query<io_stream_mode_entry, domain_v>;

        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen)
        -> connect_query_t { return connect_query_t{ std::move(*this), addr, addrlen}; }

        [[nodiscard]] auto connect(const in_addr_t addr, const uint16_t port)
        -> connect_query_t requires is_inet_domain<domain_v> {
            PEER_SIN.sin_family = domain_v;
            PEER_SIN.sin_port = htons(port);
            PEER_SIN.sin_addr.s_addr = htonl(addr);
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&PEER_SIN), sizeof(PEER_SIN)};
        }

        [[nodiscard]] auto connect(const std::string_view addr, const uint16_t port)
        -> connect_query_t requires is_inet_domain<domain_v> {
            PEER_SIN.sin_family = domain_v;
            PEER_SIN.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(PEER_SIN.sin_addr));
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&PEER_SIN), sizeof(PEER_SIN)};
        }

    };


    /**
     * @brief An @c io_entry class to represent waiting for @b binding or @b pending @b connection state
     *
     * Turns out from @c io_socket_entry as a result of processing it via @c co_await
     */
    template <int domain_v = -1, int type_v = -1>
    struct io_mapping_entry
        : io_net_entity<io_mapping_entry<domain_v, type_v>>
        , core::io_entry<io_mapping_entry<domain_v, type_v>> {

        IMPORT_IO_NET_ENTITY_ENV(io_mapping_entry)

        io_mapping_entry() : io_entity_t() {};

        explicit io_mapping_entry(const int fd) {
            io_entity_t::_fd = fd;
            if (io_entity_t::_fd > -1) io_entity_t::_is_closed = false;
        }

        struct bind_query : core::io_query<bind_query> {

            IMPORT_IO_QUERY_ENV(bind_query)

            bind_query() = delete;

            typedef io_transport_entity<domain_v, false> io_transport_entity_t;

            explicit bind_query(io_mapping_entry&& entry, sockaddr* addr, const socklen_t addrlen)
                : io_query_t(entry._fd)
                , _entry(entry)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool setup_query(core::kernel_waiter* kwp) const {
                return core::kernel_controller::bind(kwp, _fd, _addr, _addrlen);
            }

            [[nodiscard]] io_stream_mode_entry<domain_v, type_v> await_resume() {
                if constexpr (is_stream_type<type_v>)
                    return io_stream_mode_entry<domain_v, type_v>::make_from_entry(&_entry);
                else {
                    if (_res > -1) {
                        peer_sin_from(_entry->_params) = *reinterpret_cast<sockaddr_in*>(_addr);
                        return io_transport_entity_t { _res, false, _entry->_params };
                    }
                    return io_transport_entity_t {};
                }
            }

            io_mapping_entry& _entry;
            sockaddr* _addr;
            const socklen_t _addrlen;
        };

        [[nodiscard]] auto bind(const sockaddr* addr, const socklen_t addrlen)
        -> bind_query { return bind_query { std::move(*this), addr, addrlen}; }

        [[nodiscard]] auto bind(const in_addr_t addr, const uint16_t port)
        -> bind_query requires is_inet_domain<domain_v> {
            SELF_SIN.sin_family = domain_v;
            SELF_SIN.sin_port = htons(port);
            SELF_SIN.sin_addr.s_addr = htonl(addr);
            return bind_query { std::move(*this), reinterpret_cast<sockaddr*>(&SELF_SIN), sizeof(SELF_SIN)};
        }

        [[nodiscard]] auto bind(const std::string_view addr, const uint16_t port)
        -> bind_query requires is_inet_domain<domain_v> {
            SELF_SIN.sin_family = domain_v;
            SELF_SIN.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(SELF_SIN.sin_addr));
            return bind_query { std::move(*this), reinterpret_cast<sockaddr*>(&SELF_SIN), sizeof(SELF_SIN)};
        }

        using connect_query_t = connect_query<io_mapping_entry, domain_v>;

        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen)
        -> connect_query_t { return connect_query_t{ std::move(*this), addr, addrlen}; }

        [[nodiscard]] auto connect(const in_addr_t addr, const uint16_t port)
        -> connect_query_t requires is_inet_domain<domain_v> {
            PEER_SIN.sin_family = domain_v;
            PEER_SIN.sin_port = htons(port);
            PEER_SIN.sin_addr.s_addr = htonl(addr);
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&PEER_SIN), sizeof(PEER_SIN)};
        }

        [[nodiscard]] auto connect(const std::string_view addr, const uint16_t port)
        -> connect_query_t requires is_inet_domain<domain_v> {
            PEER_SIN.sin_family = domain_v;
            PEER_SIN.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(PEER_SIN.sin_addr));
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&PEER_SIN), sizeof(PEER_SIN)};
        }

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

        bool setup_query(core::kernel_waiter* kwp) const {
            core::kernel_controller::socket(kwp, domain_v, type_v, protocol_v, _flags);
            return true;
        }

        [[nodiscard]] io_mapping_entry<domain_v, type_v> await_resume() const {
            return io_mapping_entry<domain_v, type_v>{_res};
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

        bool setup_query(kernel_waiter* kwp) const {
            core::kernel_controller::socket(kwp, _domain, _type, _protocol, _flags);
            return true;
        }

        [[nodiscard]] io_mapping_entry<> await_resume() const { return io_mapping_entry{_res}; }

        const int _domain;
        const int _type;
        const int _protocol;
        const int _flags;
    };

    typedef io_listener_entity<2> io_listener;

    typedef io_transport_entity<2, false> io_net;
    typedef io_transport_entity<2, true> io_connection;

    using io_socket_raw      = io_socket<AF_INET , SOCK_RAW   , IPPROTO_RAW>;
    using io_socket_raw_dual = io_socket<AF_INET6, SOCK_RAW   , IPPROTO_RAW>;
    using io_socket_tcp      = io_socket<AF_INET , SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_tcp_dual = io_socket<AF_INET6, SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_udp      = io_socket<AF_INET , SOCK_DGRAM , IPPROTO_UDP>;
    using io_socket_udp_dual = io_socket<AF_INET6, SOCK_DGRAM , IPPROTO_UDP>;

} // end namespace ace::futures

#undef SELF_SIN
#undef PEER_SIN
#undef IMPORT_IO_NET_ENTITY_ENV
#endif //ACE_NET_H
