#ifndef ACE_NET_H
#define ACE_NET_H

#include <arpa/inet.h>
#include <netinet/in.h>

#include <ace/core/io.h>

namespace ace::net {


// ================================- META -================================


    /**
     * @brief Traits class for io net entity definition
     * @tparam derived_t Derived net entity
     */
    template <typename derived_t>
    struct io_net_entity : core::io_entity<derived_t> {

        mutable sockaddr_in _self_sin {};
        mutable sockaddr_in _peer_sin {};

        io_net_entity() = default;

        io_net_entity(io_net_entity&& io) noexcept
            : core::io_entity<derived_t>(static_cast<core::io_entity<derived_t>>(std::move(io))) { }

        io_net_entity(int fd, bool is_closed, const sockaddr_in self_sin, const sockaddr_in peer_sin) {
            core::io_entity<derived_t>::_fd = fd;
            core::io_entity<derived_t>::_is_closed = is_closed;
            _self_sin = self_sin;
            _peer_sin = peer_sin;
        }

        io_net_entity& operator =(io_net_entity&& io) noexcept {
            core::io_entity<derived_t>::_fd = io._fd;
            core::io_entity<derived_t>::_is_closed = io._is_closed;
            _self_sin = io._self_sin;
            _peer_sin = io._peer_sin;
            io._fd = -1;
            io._is_closed = true;
            return *this;
        }

    };

// NOTE: Importing names and base typename
#define IMPORT_IO_NET_ENTITY_ENV(class)                                         \
    IMPORT_IO_ENTITY_ENV(class)                                                 \
    using io_net_entity_t = io_net_entity<class>;                               \
    using io_net_entity_t::_peer_sin;                                           \
    using io_net_entity_t::_self_sin;

// NOTE: Importing basic constructors
#define IMPORT_IO_NET_ENTITY_FABRICATION using io_net_entity_t::io_net_entity_t;

    template <typename io_net_entity_t>
    concept is_net_entity = requires(io_net_entity_t entity) {
        entity._self_sin;
        entity._peer_sin;
    };

    /**
     * @brief Base class for net entity casters
     * @tparam io_net_entity_t Net entity type
     */
    template <is_net_entity io_net_entity_t>
    struct io_net_entity_caster {

        template <is_net_entity net_entity_t>
        static auto as_entity(int fd, bool is_closed, net_entity_t&& entity) {
            return io_net_entity_t { fd, is_closed, entity._self_sin, entity._peer_sin };
        }

        // template <is_net_entity net_entity_t>
        // static auto as_entity(net_entity_t&& entity) {
        //     return io_net_entity_t { entity._fd, entity._is_closed, entity._self_sin, entity._peer_sin };
        // }
    };

    template <int domain_v>
    static inline constexpr bool is_inet_domain = domain_v == AF_INET or domain_v == AF_INET6 or domain_v == PF_INET or domain_v == PF_INET6;

    template <int type_v>
    static inline constexpr bool is_stream_type = type_v == SOCK_STREAM;

    enum transport_entity_state {
        e_indirect = 0,
        e_connected = 1
    };

    template <typename, int>
    struct connect_query;


// ================================- DECLARATIONS -================================


    /**
     * @brief An @c io_entity class to represent connection socket
     *
     * Turns out from the @c io_stream_mode_entity, @c io_mapping_entity
     * or @c io_transport_entity @c [ is_connected = false ]
     * as a result of processing its member @c connect(...)
     * or the result of @c io_listener.accept(...) via @c co_await
     */
    template <int domain_v, transport_entity_state connection_state_v = e_indirect>
    struct io_transport_entity;

    /**
     * @brief An @c io_entity class to represent listen socket
     *
     * Turns out from the @c io_stream_mode_entity as a result of processing its member @c listen()
     * via @c co_await
     */
    template <int domain_v>
    struct io_listener_entity;

    /**
     * @brief An @c io_entity class to represent socket mode selection [ @b Listener | @b Connection ]
     *
     * Turns out from the @c io_mapping_entity only for the @b SOCK_STREAM socket type
     * as a result of processing its member @c bind(...) via @c co_await
     */
    template <int domain_v, int type_v>
    struct io_stream_mode_entity;

    /**
     * @brief An @c io_entity class to represent waiting for @b binding or @b pending @b connection state
     *
     * Turns out from @c io_socket_entity as a result of processing it via @c co_await
     */
    template <int domain_v, int type_v>
    struct io_mapping_entity;

    /**
     * @brief An @b io_entity for socket creation. Also, supports aliasing
     * @tparam domain_v Communication domain
     * @tparam type_v Communication semantics
     * @tparam protocol_v Particular socket protocol
     */
    template <int domain_v, int type_v, int protocol_v>
    struct io_socket;


// ================================- ALIASES -================================


    typedef io_listener_entity<2> io_listener;

    typedef io_transport_entity<2, e_indirect> io_net_interface;
    typedef io_transport_entity<2, e_connected> io_connection;

    using io_socket_raw      = io_socket<AF_INET , SOCK_RAW   , IPPROTO_RAW>;
    using io_socket_raw_v6   = io_socket<AF_INET6, SOCK_RAW   , IPPROTO_RAW>;
    using io_socket_tcp      = io_socket<AF_INET , SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_tcp_v6   = io_socket<AF_INET6, SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_udp      = io_socket<AF_INET , SOCK_DGRAM , IPPROTO_UDP>;
    using io_socket_udp_v6   = io_socket<AF_INET6, SOCK_DGRAM , IPPROTO_UDP>;

}


// ================================- CASTERS -================================


    /**
     * @base @c io_transport_entity caster specialization for fabricating it from another io_net_entities
     */
    template<int domain_v, ace::net::transport_entity_state connection_state_v>
    struct ace::core::io_caster<ace::net::io_transport_entity<domain_v, connection_state_v>>
        : net::io_net_entity_caster<net::io_transport_entity<domain_v, connection_state_v>> {
        using net::io_net_entity_caster<net::io_transport_entity<domain_v, connection_state_v>>::as_entity;
    };

    /**
     * @base @c io_listener_entity caster specialization for fabricating it from another io_net_entities
     */
    template<int domain_v>
    struct ace::core::io_caster<ace::net::io_listener_entity<domain_v>>
        : net::io_net_entity_caster<net::io_listener_entity<domain_v>> {
        using net::io_net_entity_caster<net::io_listener_entity<domain_v>>::as_entity;
    };

    /**
     * @base @c io_stream_mode_entity caster specialization for fabricating it from another io_net_entities
     */
    template<int domain_v, int type_v>
    struct ace::core::io_caster<ace::net::io_stream_mode_entity<domain_v, type_v>>
        : net::io_net_entity_caster<net::io_stream_mode_entity<domain_v, type_v>> {
        using net::io_net_entity_caster<net::io_stream_mode_entity<domain_v, type_v>>::as_entity;
    };

    /**
     * @base @c io_mapping_entity caster specialization for fabricating it from another io_net_entities
     */
    template<int domain_v, int type_v>
    struct ace::core::io_caster<ace::net::io_mapping_entity<domain_v, type_v>>
        : net::io_net_entity_caster<net::io_mapping_entity<domain_v, type_v>> {
        using net::io_net_entity_caster<net::io_mapping_entity<domain_v, type_v>>::as_entity;
    };


// ================================- DEFINITIONS -================================


    template <int domain_v, ace::net::transport_entity_state connection_state_v>
    struct ace::net::io_transport_entity : io_net_entity<io_transport_entity<domain_v, connection_state_v>> {

        IMPORT_IO_NET_ENTITY_ENV(io_transport_entity)
        IMPORT_IO_NET_ENTITY_FABRICATION

        io_transport_entity() = default;

        using connect_query_t = connect_query<io_transport_entity, domain_v>;
        friend connect_query_t;

        struct send_query : core::io_query<send_query> {

            IMPORT_IO_QUERY_ENV(send_query);

            send_query() = delete;

            explicit send_query(const int fd, const void *buf, const size_t len, const int flags = 0)
                : io_query_t(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags) {}

            bool setup_query(core::services::kernel_observer* kwp) const {
                return core::services::kernel_controller::send(kwp, _fd, _buf, _len, _flags);
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

            bool setup_query(core::services::kernel_observer* kwp) const {
                return core::services::kernel_controller::sendto(kwp, _fd, _buf, _len, _flags, _addr, _addrlen);
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

            bool setup_query(core::services::kernel_observer* kwp) const {
                return core::services::kernel_controller::recv(kwp, _fd, _buf, _len, _flags);
            }

            [[nodiscard]] int await_resume() const { return _res; }

            void *_buf;
            const size_t _len;
            const int _flags;
        };

        [[nodiscard]] auto send(const void *buf, const size_t len, const int flags = 0) const
        -> send_query requires (connection_state_v == e_connected)
        { return send_query{_fd, buf, len, flags}; }

        [[nodiscard]] auto send(const std::string_view buf, const int flags = 0) const
        -> send_query requires (connection_state_v == e_connected)
        { return send_query{_fd, buf.data(), buf.size(), flags}; }

        template <typename data_t>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto send(const std::vector<data_t>& buf, const int flags = 0) const
        -> send_query requires (connection_state_v == e_connected)
        { return send_query{_fd, buf.data(), buf.size() * (sizeof(data_t) / sizeof(char)), flags}; }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto send(const std::array<data_t, len_v>& buf, const int flags = 0) const
        -> send_query requires (connection_state_v == e_connected)
        { return send_query{_fd, buf.data(), len_v * (sizeof(data_t) / sizeof(char)), flags}; }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto send(const std::span<data_t, len_v>& buf, const int flags = 0) const
        -> send_query requires (connection_state_v == e_connected)
        { return send_query{_fd, buf.data(), buf.size_bytes(), flags}; }

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen)
        -> connect_query_t requires (connection_state_v == e_indirect)
        { return connect_query_t{ std::move(*this), addr, addrlen}; }

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const in_addr_t addr, const uint16_t port)
        -> connect_query_t requires (is_inet_domain<domain_v> and connection_state_v == e_indirect) {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            _peer_sin.sin_addr.s_addr = htonl(addr);
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&_peer_sin), sizeof(sockaddr_in)};
        }

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const std::string_view addr, const uint16_t port)
        -> connect_query_t requires (is_inet_domain<domain_v> and connection_state_v == e_indirect) {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_peer_sin.sin_addr));
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&_peer_sin), sizeof(sockaddr_in)};
        }

        [[nodiscard]] auto sendto(const void *buf, const size_t len, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect)
        { return sendto_query{_fd, buf, len, flags, addr, addrlen}; }

        [[nodiscard]] auto sendto(const std::string_view buf, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect)
        { return sendto_query{_fd, buf.data(), buf.size(), flags, addr, addrlen}; }

        template <typename data_t>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto sendto(const std::vector<data_t>& buf, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect)
        { return sendto_query{_fd, buf.data(), buf.size() * (sizeof(data_t) / sizeof(char)), flags, addr, addrlen}; }

        [[nodiscard]] auto sendto(const std::string& buf, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect)
        { return sendto_query{_fd, buf.data(), buf.size(), flags, addr, addrlen}; }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto sendto(const std::array<data_t, len_v>& buf, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect)
        { return sendto_query{_fd, buf.data(), len_v * (sizeof(data_t) / sizeof(char)), flags, addr, addrlen}; }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto sendto(const std::span<data_t, len_v>& buf, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect)
        { return sendto_query{_fd, buf.data(), buf.size_bytes(), flags, addr, addrlen}; }

        [[nodiscard]] auto recv(void *buf, const size_t len, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf, len, flags}; }

        template <typename data_t>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto recv(std::vector<data_t>& buf, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf.data(), buf.capacity() * (sizeof(data_t) / sizeof(char)), flags}; }

        [[nodiscard]] auto recv(std::string& buf, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf.data(), buf.capacity(), flags}; }

        template <typename data_t>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto recv_vec(const int flags = 0) const
        -> promise<std::expected<std::vector<data_t>, int>> {
            static constexpr int buff_len = 128;
            static constexpr int buff_len_bytes = buff_len * (sizeof(data_t) / sizeof(char));

            std::deque<std::array<data_t, buff_len>> acc;
            int total = 0;

            auto& buff = acc.emplace_back();
            int bytes_read = co_await recv_query(_fd, buff.data(), buff_len_bytes, flags);
            if (bytes_read < 0) co_return std::unexpected(-bytes_read);
            total += bytes_read;

            while (bytes_read == buff_len) {
                buff = acc.emplace_back();
                bytes_read = co_await recv_query(_fd, buff.data(), buff_len_bytes, flags);
                if (bytes_read < 0) co_return std::unexpected(-bytes_read);
                total += bytes_read;
            }

            // NOTE: Cast to data object size
            total /= (sizeof(data_t) / sizeof(char));
            std::vector<data_t> res {};
            res.reserve(total);
            for (auto& buf : acc) {
                const int write_items { (total > buff_len) ? buff_len : total };
                for (int i = 0; i < write_items; ++i)
                    res.push_back(std::forward<data_t>(buf[i]));
                total -= write_items;
            }
            co_return res;
        }

        [[nodiscard]] auto recv_str(const int flags = 0) const
        -> promise<std::expected<std::string, int>> {
            static constexpr int buff_len = 128;

            std::deque<std::array<char, buff_len>> acc {};
            int total = 0;

            auto& buff = acc.emplace_back();
            int bytes_read = co_await recv_query(_fd, buff.data(), buff_len, flags);
            if (bytes_read < 0) co_return std::unexpected(-bytes_read);
            total += bytes_read;

            while (bytes_read == buff_len) {
                buff = acc.emplace_back();
                bytes_read = co_await recv_query(_fd, buff.data(), buff_len, flags);
                if (bytes_read < 0) co_return std::unexpected(-bytes_read);
                total += bytes_read;
            }

            std::string res {};
            // NOTE: + null term char slot
            res.reserve(total + 1);
            for (auto& buf : acc) {
                const int write_bytes { (total > buff_len) ? buff_len : total };
                res.append(buf.data(), write_bytes);
                total -= write_bytes;
            }
            co_return res;
        }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto recv(std::array<data_t, len_v>& buf, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf.data(), len_v * (sizeof(data_t) / sizeof(char)), flags}; }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto recv(std::span<data_t, len_v>& buf, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf.data(), buf.size_bytes(), flags}; }

    };

    template <typename entity_t, int domain_v>
    struct ace::net::connect_query : core::io_query<connect_query<entity_t, domain_v>> {

        IMPORT_IO_QUERY_ENV(connect_query)

        connect_query() = delete;

        typedef io_transport_entity<domain_v, e_connected> io_transport_entity_t;

        explicit connect_query(entity_t&& entity, const sockaddr* addr, const socklen_t addrlen)
            : io_query_t(entity._fd)
            , _entity(entity)
            , _addr(addr)
            , _addrlen(addrlen) {}

        bool setup_query(core::services::kernel_observer* kwp) const {
            return core::services::kernel_controller::connect(kwp, _fd, _addr, _addrlen);
        }

        [[nodiscard]] io_transport_entity_t await_resume() const {
            if (_res > -1) {
                return io_transport_entity_t::consume(_entity);
            }
            return io_transport_entity_t {};
        }

        entity_t& _entity;
        const sockaddr* _addr;
        const socklen_t _addrlen;
    };


    template <int domain_v>
    struct ace::net::io_listener_entity : io_net_entity<io_listener_entity<domain_v>> {

        IMPORT_IO_NET_ENTITY_ENV(io_listener_entity);
        IMPORT_IO_NET_ENTITY_FABRICATION

        io_listener_entity() = default;

        friend core::io_caster<io_listener_entity>;

        struct accept_query : core::io_query<accept_query> {

            IMPORT_IO_QUERY_ENV(accept_query)

            accept_query() = delete;

            typedef io_transport_entity<domain_v, e_connected> io_transport_entity_t;

            explicit accept_query(const io_listener_entity* entity, sockaddr* addr, socklen_t* addrlen, const int flags = 0)
                : io_query_t(entity->_fd)
                , _entity(entity)
                , _addr(addr)
                , _addrlen(addrlen)
                , _flags(flags) {}

            bool setup_query(core::services::kernel_observer* kwp) const {
                return core::services::kernel_controller::accept(kwp, _fd, _addr, _addrlen, _flags);
            }

            [[nodiscard]] io_transport_entity_t await_resume() const {
                if (_res > -1) {
                    _entity->_peer_sin = *reinterpret_cast<sockaddr_in*>(_addr);
                    return core::io_caster<io_transport_entity_t>::as_entity(_res, false, std::move(*_entity));
                }
                return io_transport_entity_t {};
            }

            const io_listener_entity* _entity;
            sockaddr* _addr;
            socklen_t* _addrlen;
            const int _flags;
        };

        [[nodiscard]] auto accept()
        -> accept_query { return accept_query { this, reinterpret_cast<sockaddr*>(&_self_sin), &_self_sin_size}; }

        [[nodiscard]] auto accept(sockaddr* addr, const socklen_t* addrlen, const int flags = 0)
        -> accept_query { return accept_query{this, addr, addrlen, flags}; }

        [[nodiscard]] auto accept(const in_addr_t addr, const uint16_t port)
        -> accept_query requires is_inet_domain<domain_v> {
            _self_sin.sin_family = domain_v;
            _self_sin.sin_port = htons(port);
            _self_sin.sin_addr.s_addr = htonl(addr);
            return accept_query { this, reinterpret_cast<sockaddr*>(&_self_sin), &_self_sin_size};
        }

        [[nodiscard]] auto accept(const std::string_view addr, const uint16_t port)
        -> accept_query requires is_inet_domain<domain_v> {
            _self_sin.sin_family = domain_v;
            _self_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_self_sin.sin_addr));
            return accept_query { this, reinterpret_cast<sockaddr*>(&_self_sin), _self_sin_len_ptr};
        }

        socklen_t _self_sin_size = sizeof(sockaddr_in);
        socklen_t* _self_sin_len_ptr = &_self_sin_size;

    };


    template <int domain_v, int type_v>
    struct ace::net::io_stream_mode_entity : io_net_entity<io_stream_mode_entity<domain_v, type_v>> {

        IMPORT_IO_NET_ENTITY_ENV(io_stream_mode_entity)
        IMPORT_IO_NET_ENTITY_FABRICATION

        io_stream_mode_entity() : io_entity_t() {};

        typedef io_listener_entity<domain_v> io_listener_entity_t;

        struct listen_query : core::io_query<listen_query> {

            IMPORT_IO_QUERY_ENV(listen_query)

            listen_query() = delete;

            explicit listen_query(io_stream_mode_entity&& entity, const int backlog)
                : io_query_t(entity._fd)
                , _entity(entity)
                , _backlog(backlog) {}

            bool setup_query(core::services::kernel_observer* kwp) const {
                return core::services::kernel_controller::listen(kwp, _fd, _backlog);
            }

            [[nodiscard]] io_listener_entity_t await_resume() const {
                return io_listener_entity_t::consume(_entity);
            }

            io_stream_mode_entity& _entity;
            const int _backlog;
        };


        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto listen(const int backlog = 0)
        -> listen_query requires (type_v == SOCK_SEQPACKET or type_v == SOCK_STREAM) {
            return listen_query{ std::move(*this), backlog};
        }

        using connect_query_t = connect_query<io_stream_mode_entity, domain_v>;
        friend connect_query_t;

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen)
        -> connect_query_t { return connect_query_t{ std::move(*this), addr, addrlen}; }

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const in_addr_t addr, const uint16_t port)
        -> connect_query_t requires is_inet_domain<domain_v> {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            _peer_sin.sin_addr.s_addr = htonl(addr);
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&_peer_sin), sizeof(sockaddr_in)};
        }

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const std::string_view addr, const uint16_t port)
        -> connect_query_t requires is_inet_domain<domain_v> {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_peer_sin.sin_addr));
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&_peer_sin), sizeof(sockaddr_in)};
        }

    };


    template <int domain_v, int type_v>
    struct ace::net::io_mapping_entity : io_net_entity<io_mapping_entity<domain_v, type_v>> {

        IMPORT_IO_NET_ENTITY_ENV(io_mapping_entity)
        IMPORT_IO_NET_ENTITY_FABRICATION

        io_mapping_entity() : io_entity_t() {};

        explicit io_mapping_entity(const int fd) {
            io_entity_t::_fd = fd;
            if (io_entity_t::_fd > -1) io_entity_t::_is_closed = false;
        }

        struct bind_query : core::io_query<bind_query> {

            IMPORT_IO_QUERY_ENV(bind_query)

            bind_query() = delete;

            typedef io_transport_entity<domain_v, e_indirect> io_transport_entity_t;

            typedef io_stream_mode_entity<domain_v, type_v> io_stream_mode_entity_t;

            explicit bind_query(io_mapping_entity&& entity, sockaddr* addr, const socklen_t addrlen)
                : io_query_t(entity._fd)
                , _entity(entity)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool setup_query(core::services::kernel_observer* kwp) const {
                return core::services::kernel_controller::bind(kwp, _fd, _addr, _addrlen);
            }

            [[nodiscard]] io_stream_mode_entity_t await_resume() {
                if constexpr (is_stream_type<type_v>)
                    return io_stream_mode_entity_t::consume(_entity);
                else {
                    if (_res > -1) {
                        _entity._peer_sin = *reinterpret_cast<sockaddr_in*>(_addr);
                        return io_transport_entity_t { _res, false, std::move(*_entity) };
                    }
                    return io_transport_entity_t {};
                }
            }

            io_mapping_entity& _entity;
            sockaddr* _addr;
            const socklen_t _addrlen;
        };

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto bind(const sockaddr* addr, const socklen_t addrlen)
        -> bind_query { return bind_query { std::move(*this), addr, addrlen}; }

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto bind(const in_addr_t addr, const uint16_t port)
        -> bind_query requires is_inet_domain<domain_v> {
            _self_sin.sin_family = domain_v;
            _self_sin.sin_port = htons(port);
            _self_sin.sin_addr.s_addr = htonl(addr);
            return bind_query { std::move(*this), reinterpret_cast<sockaddr*>(&_self_sin), sizeof(sockaddr_in)};
        }

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto bind(const std::string_view addr, const uint16_t port)
        -> bind_query requires is_inet_domain<domain_v> {
            _self_sin.sin_family = domain_v;
            _self_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_self_sin.sin_addr));
            return bind_query { std::move(*this), reinterpret_cast<sockaddr*>(&_self_sin), sizeof(sockaddr_in)};
        }

        using connect_query_t = connect_query<io_mapping_entity, domain_v>;
        friend connect_query_t;

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen)
        -> connect_query_t { return connect_query_t{ std::move(*this), addr, addrlen}; }

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const in_addr_t addr, const uint16_t port)
        -> connect_query_t requires is_inet_domain<domain_v> {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            _peer_sin.sin_addr.s_addr = htonl(addr);
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&_peer_sin), sizeof(sockaddr_in)};
        }

        /**
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const std::string_view addr, const uint16_t port)
        -> connect_query_t requires is_inet_domain<domain_v> {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_peer_sin.sin_addr));
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&_peer_sin), sizeof(sockaddr_in)};
        }

    };

    template <int domain_v, int type_v, int protocol_v>
    struct ace::net::io_socket : core::io_query<io_socket<domain_v, type_v, protocol_v>> {

        IMPORT_IO_QUERY_ENV(io_socket)

        /**
         * @param [in] flags currently unused
         */
        explicit io_socket(const int flags = 0)
            // NOTE: There is no socket but need supress defaulted '-1' errcode
            : io_query_t(0)
            , _flags(flags) {}

        bool setup_query(core::services::kernel_observer* kwp) const {
            core::services::kernel_controller::socket(kwp, domain_v, type_v, protocol_v, _flags);
            return true;
        }

        [[nodiscard]] io_mapping_entity<domain_v, type_v> await_resume() const {
            return io_mapping_entity<domain_v, type_v>{_res};
        }

        const int _flags;
    };


#undef IMPORT_IO_NET_ENTITY_ENV
#undef IMPORT_IO_NET_ENTITY_FABRICATION
#endif //ACE_NET_H
