/**
 * @file net.h
 * @brief Asynchronous network I/O — sockets, listeners, and connections built
 *        on top of @c io_uring via the ACE I/O framework.
 *
 * @details This header provides a type-safe, RAII-based networking stack
 * where each socket lifecycle stage is represented by a distinct type.  The
 * state machine prevents misuse at compile time (e.g., you cannot @c send()
 * on a socket that hasn't been connected).
 *
 * ### Entity state machine
 *
 * @mermaid{ graph LR; Socket[\"socket\"]-->Mapping[\"socket_entity\"]; Mapping-->|bind()|Stream[\"stream_mode_entity\"]; Mapping-->|bind()|Interface[\"net_interface\"]; Mapping-->|connect()|Connection[\"connection\"]; Stream-->|listen()|Listener[\"listener_entity\"]; Stream-->|connect()|Connection; Listener-->|accept()|Connection; }
 *
 * ### Key types (from creation to data transfer)
 *
 * | Type | Role |
 * |---|---|
 * | @c socket<domain,type,proto> | Creates a raw socket — awaitable, produces @c socket_entity |
 * | @c socket_entity<domain,type> | Ready for @c bind() or @c connect() |
 * | @c stream_mode_entity<domain,type> | After @c bind() (@b SOCK_STREAM) — choose @c listen() or @c connect() |
 * | @c listener_entity<domain> | Listening — produces connections via @c accept() |
 * | @c transport_entity<domain,e_indirect> | Bound but not connected (@b SOCK_DGRAM) — supports @c sendto()/@c recv() |
 * | @c transport_entity<domain,e_connected> | Connected — supports @c send()/@c recv() |
 * | @c connection_link | Higher-level @c io::link for connected sockets (@c write/@c read) |
 *
 * ### Convenience aliases
 *
 * | Alias | Expansion |
 * |---|---|
 * | @c socket_tcp | @c socket<AF_INET, SOCK_STREAM, IPPROTO_TCP> |
 * | @c socket_udp | @c socket<AF_INET, SOCK_DGRAM, IPPROTO_UDP> |
 * | @c listener | @c listener_entity<AF_INET> |
 * | @c net_interface | @c transport_entity<AF_INET, e_indirect> |
 * | @c connection | @c transport_entity<AF_INET, e_connected> |
 *
 * @see ace::io::entity, ace::io::link, ace::services::kernel_controller
 */
#ifndef ACE_NET_H
#define ACE_NET_H


#include <vector>
#include <string>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <ace/io.h>

namespace ace::net {


// ================================- META -================================


    /**
     * @brief Traits class for io net entity definition
     * @tparam derived_t Derived net entity
     */
    template <typename derived_t>
    struct net_entity : io::entity<derived_t> {

        mutable sockaddr_in _self_sin {};  ///< Address the socket is bound to.
        mutable sockaddr_in _peer_sin {};  ///< Address of the remote peer.

        /** @brief Default constructor. */
        net_entity() = default;

        /**
         * @brief Move constructor that transfers sole FD ownership and addresses.
         * @param io Source entity; its FD ownership is invalidated by the base move.
         */
        net_entity(net_entity&& io) noexcept
            : io::entity<derived_t>(std::move(io))
            , _self_sin(io._self_sin)
            , _peer_sin(io._peer_sin) {}

        /** @brief Constructs an entity from a raw fd. */
        net_entity(int fd, bool is_closed) {
            io::entity<derived_t>::_fd = fd;
            io::entity<derived_t>::_is_closed = is_closed;
        }

        /** @brief Constructs an entity from a raw fd and addresses. */
        net_entity(int fd, bool is_closed, const sockaddr_in self_sin, const sockaddr_in peer_sin) {
            io::entity<derived_t>::_fd = fd;
            io::entity<derived_t>::_is_closed = is_closed;
            _self_sin = self_sin;
            _peer_sin = peer_sin;
        }

        /**
         * @brief Move assignment that releases the current FD, then transfers
         *        sole FD ownership and addresses.
         * @details Delegates FD ownership and self-move handling to @c io::entity.
         * @param io Source entity; its FD ownership is invalidated unless this is
         *           a self-move.
         * @return This entity.
         */
        net_entity& operator =(net_entity&& io) noexcept {
            io::entity<derived_t>::operator=(std::move(io));
            _self_sin = io._self_sin;
            _peer_sin = io._peer_sin;
            return *this;
        }

    };

// NOTE: Importing names and base typename
/** @brief Imports the @c net_entity environment: base aliases, peer and self addresses. */
#define IMPORT_IO_NET_ENTITY_ENV(class)                                         \
    IMPORT_IO_ENTITY_ENV(class)                                                 \
    using io_net_entity_t = net_entity<class>;                                  \
    using io_net_entity_t::_peer_sin;                                           \
    using io_net_entity_t::_self_sin;

// NOTE: Importing basic constructors
/** @brief Imports the @c net_entity base constructors. */
#define IMPORT_IO_NET_ENTITY_FABRICATION using io_net_entity_t::io_net_entity_t;

    /** @brief Concept: type exposes @c _self_sin and @c _peer_sin address fields. */
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
    struct net_entity_caster {

        /**
         * @brief Fabricates a net entity from a raw fd and a source entity's addresses.
         * @tparam net_entity_t  Source net entity type.
         * @param fd        File descriptor of the new entity.
         * @param is_closed Whether the descriptor is closed.
         * @param entity    Source entity whose addresses are copied.
         */
        template <is_net_entity net_entity_t>
        static auto from_entity(int fd, bool is_closed, net_entity_t&& entity) {
            return io_net_entity_t { fd, is_closed, entity._self_sin, entity._peer_sin };
        }
    };

    /** @brief @c true when the domain is an IPv4/IPv6 inet family. */
    template <int domain_v>
    static inline constexpr bool is_inet_domain = domain_v == AF_INET or domain_v == AF_INET6 or domain_v == PF_INET or domain_v == PF_INET6;

    /** @brief @c true when the socket type is @c SOCK_STREAM. */
    template <int type_v>
    static inline constexpr bool is_stream_type = type_v == SOCK_STREAM;

    /**
     * @brief Ordered state of a network transport entity.
     */
    enum transport_entity_state {
        e_indirect = 0,  ///< Not connected (datagram interface) — sendto/recv mode.
        e_connected = 1  ///< Connected — send/recv mode.
    };

    /**
     * @brief Awaitable query for connecting a socket to a remote address.
     * @tparam entity_t  The source entity type (consumed on resume).
     * @tparam domain_v  Address family (AF_INET, AF_INET6, etc.).
     */
    template <typename, int>
    struct connect_query;

    /**
     * @brief Awaitable query for sending data over a connected socket.
     */
    struct send_query;

    /**
     * @brief Awaitable query for receiving data into a caller-owned buffer.
     */
    struct recv_query;


// ================================- DECLARATIONS -================================


    /**
     * @brief @c io::link implementation for connected sockets.
     *
     * @details Implements @c output_action() via async @c sendmsg() (with
     * fallback to blocking @c ::sendmsg()) and @c input_action() via the
     * cancelable @c io_uring receive query.
     */
    struct connection_link;

    /**
     * @brief An @c io::entity class to represent connection socket
     *
     * Turns out from the @c stream_mode_entity, @c socket_entity
     * or @c transport_entity as a result of processing its member
     * @c connect(...), or from @c listener.accept(...) via @c co_await,
     * producing the @c e_connected state.
     */
    template <int domain_v, transport_entity_state connection_state_v = e_indirect>
    struct transport_entity;

    /**
     * @brief An @c io::entity class to represent listen socket
     *
     * Turns out from the @c stream_mode_entity as a result of processing its member @c listen()
     * via @c co_await
     */
    template <int domain_v>
    struct listener_entity;

    /**
     * @brief An @c io::entity class to represent socket mode selection [ @b Listener | @b Connection ]
     *
     * Turns out from the @c socket_entity only for the @b SOCK_STREAM socket type
     * as a result of processing its member @c bind(...) via @c co_await
     */
    template <int domain_v, int type_v>
    struct stream_mode_entity;

    /**
     * @brief An @c io::entity class to represent waiting for @b binding or @b pending @b connection state
     *
     * Turns out from @c socket as a result of processing it via @c co_await
     */
    template <int domain_v, int type_v>
    struct socket_entity;

    /**
     * @brief An @b io::entity for socket creation. Also, supports aliasing
     * @tparam domain_v Communication domain
     * @tparam type_v Communication semantics
     * @tparam protocol_v Particular socket protocol
     */
    template <int domain_v, int type_v, int protocol_v>
    struct socket;


// ================================- ALIASES -================================


    typedef listener_entity<2> listener;                        ///< IPv4 listening socket alias.

    typedef transport_entity<2, e_indirect> net_interface;      ///< IPv4 datagram interface alias.
    typedef transport_entity<2, e_connected> connection;        ///< IPv4 connected socket alias.

    using socket_raw      = socket<AF_INET , SOCK_RAW   , IPPROTO_RAW>;  ///< Raw IPv4 socket alias.
    using socket_raw_v6   = socket<AF_INET6, SOCK_RAW   , IPPROTO_RAW>;  ///< Raw IPv6 socket alias.
    using socket_tcp      = socket<AF_INET , SOCK_STREAM, IPPROTO_TCP>;  ///< TCP IPv4 socket alias.
    using socket_tcp_v6   = socket<AF_INET6, SOCK_STREAM, IPPROTO_TCP>;  ///< TCP IPv6 socket alias.
    using socket_udp      = socket<AF_INET , SOCK_DGRAM , IPPROTO_UDP>;  ///< UDP IPv4 socket alias.
    using socket_udp_v6   = socket<AF_INET6, SOCK_DGRAM , IPPROTO_UDP>;  ///< UDP IPv6 socket alias.

}

    /**
     * @brief Awaitable that connects a socket entity to a remote address.
     *
     * @details Consumes the source entity on success and produces a
     * @c transport_entity in the connected state.
     *
     * @tparam entity_t  Source entity type.
     * @tparam domain_v  Address family.
     */
    template <typename entity_t, int domain_v>
    struct ace::net::connect_query : io::query<connect_query<entity_t, domain_v>> {

        IMPORT_IO_QUERY_ENV(connect_query)

        /** @brief Deleted: a connect query requires an entity and an address. */
        connect_query() = delete;

        /// @brief Resulting connected transport entity type.
        typedef transport_entity<domain_v, e_connected> io_transport_entity_t;

        /**
         * @brief Constructs a connect query from a source entity.
         * @param entity   Source entity (consumed on resume).
         * @param addr     Remote address to connect to.
         * @param addrlen  Length of @p addr.
         */
        explicit connect_query(entity_t&& entity, const sockaddr* addr, const socklen_t addrlen)
            : io_query_t(entity._fd)
            , _entity(entity)
            , _addr(addr)
            , _addrlen(addrlen) {}

        /** @brief Submits the connect operation to the kernel controller. */
        bool setup_query(services::kernel_observer* kwp) const {
            return services::kernel_controller::connect(kwp, _fd, _addr, _addrlen);
        }

        /**
         * @brief Returns the connected transport entity on success.
         * @return Connected entity, or a failed entity carrying the error code.
         */
        [[nodiscard]] io_transport_entity_t await_resume() const {
            if (_res > -1) {
                return io_transport_entity_t::consume(_entity);
            }
            return io_transport_entity_t {_res, true};
        }

        entity_t& _entity;      ///< Reference to the source entity being consumed.
        const sockaddr* _addr;  ///< Remote address to connect to.
        const socklen_t _addrlen;  ///< Length of @c _addr.
    };

    /**
     * @brief Awaitable that sends data on a connected transport entity.
     *
     * @details @c co_await resolves to the number of bytes sent.  A length
     * above @c kernel_controller::max_io_length completes without submission
     * and returns @c -EOVERFLOW.
     */
    struct ace::net::send_query : io::query<send_query> {

        IMPORT_IO_QUERY_ENV(send_query);

        /** @brief Deleted: a send query requires a fd and a buffer. */
        send_query() = delete;

        /**
         * @brief Constructs a send query.
         * @param fd     Socket descriptor.
         * @param buf    Data buffer to send.
         * @param len    Buffer length.
         * @param flags  Send flags (default 0).
         */
        explicit send_query(const int fd, const void *buf, const size_t len, const int flags = 0)
            : io_query_t(fd)
            , _buf(buf)
            , _len(len)
            , _flags(flags) {
            if (not services::kernel_controller::is_io_length_supported(len))
                fail_before_submission(EOVERFLOW);
        }

        /** @brief Submits the send operation to the kernel controller. */
        bool setup_query(services::kernel_observer* kwp) const {
            return services::kernel_controller::send(kwp, _fd, _buf, _len, _flags);
        }

        /** @brief Returns the number of bytes sent (or a negative error code). */
        [[nodiscard]] int await_resume() const { return _res; }

        const void *_buf;   ///< Data buffer to send.
        const size_t _len;  ///< Buffer length.
        const int _flags;   ///< Send flags.
    };


    /**
     * @brief Awaitable query for receiving data.
     *
     * @details A length above @c kernel_controller::max_io_length completes
     * without submission and returns @c -EOVERFLOW from @c await_resume().
     * The destination must remain writable and alive until completion or
     * cancellation.
     */
    struct ace::net::recv_query : io::query<recv_query> {

        IMPORT_IO_QUERY_ENV(recv_query)

        /** @brief Deleted: a recv query requires a fd and a buffer. */
        recv_query() = delete;

        /**
         * @brief Constructs a receive query.
         * @param fd Socket descriptor.
         * @param buf Destination buffer.
         * @param len Writable buffer size in bytes.
         * @param flags Receive flags (default 0).
         */
        explicit recv_query(const int fd, void *buf, const size_t len, const int flags = 0)
            : io_query_t(fd)
            , _buf(buf)
            , _len(len)
            , _flags(flags) {
            if (not services::kernel_controller::is_io_length_supported(len))
                fail_before_submission(EOVERFLOW);
        }

        /** @brief Submits the receive operation to the kernel controller. */
        bool setup_query(services::kernel_observer* kwp) const {
            return services::kernel_controller::recv(kwp, _fd, _buf, _len, _flags);
        }

        /** @brief Returns the number of bytes received or a negative errno. */
        [[nodiscard]] int await_resume() const { return _res; }

        void *_buf;          ///< Destination buffer.
        const size_t _len;   ///< Writable buffer size in bytes.
        const int _flags;    ///< Receive flags.
    };


    /**
     * @brief High-level connection wrapper over @c io::link.
     *
     * @details Fire-and-forget @c write()/@c writeln() via scatter-gather
     * @c sendmsg; cancelable io_uring receives for @c read()/@c read_buf().
     */
    struct ace::net::connection_link : io::link {

        IMPORT_IO_LINK_ENV(connection_link);
        IMPORT_IO_LINK_FABRICATION;

    protected:

        /**
         * @brief Asynchronously writes a buffer to the socket via @c sendmsg,
         *        with a blocking @c ::sendmsg fallback.
         * @param buff  Scatter-gather buffer to send.
         */
        void output_action(io::buffer&& buff) override {
            // NOTE: Trying to get current runner.
            // NOTE: Doing it manually for cases when classic 'runner::run()' is unused
            auto* runner_identity = core::runner::get().as<runner_pool_t>();
            // NOTE: Pushing data to slot, and setting identity for kernelic
            if (io::outcast::command* cmd {}; runner_identity and io::outcast::_command_pool.capture(cmd)) [[likely]]
            {
                cmd->_runner_identity = runner_identity;
                cmd->_buffer = std::move(buff);
                cmd->_description = "net::connection_link lazy-send";
                const auto* assembled = cmd->_buffer.assemble();
                if (services::kernel_controller::sendmsg(cmd, _fd, assembled, 0))
                    return;
            }
            // NOTE: If can not get slot or identity not found -> using busy behavior
            const auto* assembled = buff.assemble();
            if (::sendmsg(_fd, assembled, 0) < 0 and io::outcast::fail_cb_handler)
                io::outcast::fail_cb_handler(errno, "net::connection_link busy-send");
        };

        /**
         * @brief Asynchronously reads data from the socket.
         * @param buff  Destination buffer.
         * @param len   Buffer length.
         * @return Number of bytes read or a negative errno value.
         * @warning @p buff must remain alive until completion or cancellation.
         */
        promise<int> input_action(void *buff, std::size_t len) override {
            co_return co_await recv_query {_fd, buff, len, 0};
        }

    public:

        /** @brief Default constructor. */
        connection_link() = default;

    };


// ================================- CASTERS -================================


    /**
     * @brief @c transport_entity caster specialization for fabricating it from another net entities
     */
    template<int domain_v, ace::net::transport_entity_state connection_state_v>
    struct ace::io::caster<ace::net::transport_entity<domain_v, connection_state_v>>
        : net::net_entity_caster<net::transport_entity<domain_v, connection_state_v>> {
        using net::net_entity_caster<net::transport_entity<domain_v, connection_state_v>>::from_entity;

        /**
         * @brief Fabricates a @c connection_link from a connected entity.
         * @param fd        File descriptor of the connection.
         * @param is_closed Whether the descriptor is closed.
         * @param entity    Source entity carrying the addresses.
         */
        template <net::is_net_entity net_entity_t>
        static auto as_link(const int fd, const bool is_closed, net_entity_t&& entity)
        requires (connection_state_v == net::e_connected) {
            return net::connection_link { fd, is_closed, std::forward<net_entity_t>(entity) };
        }
    };


    /**
     * @brief @c listener_entity caster specialization for fabricating it from another net entities
     */
    template<int domain_v>
    struct ace::io::caster<ace::net::listener_entity<domain_v>>
        : net::net_entity_caster<net::listener_entity<domain_v>> {
        using net::net_entity_caster<net::listener_entity<domain_v>>::from_entity;
    };


    /**
     * @brief @c stream_mode_entity caster specialization for fabricating it from another net entities
     */
    template<int domain_v, int type_v>
    struct ace::io::caster<ace::net::stream_mode_entity<domain_v, type_v>>
        : net::net_entity_caster<net::stream_mode_entity<domain_v, type_v>> {
        using net::net_entity_caster<net::stream_mode_entity<domain_v, type_v>>::from_entity;
    };


    /**
     * @brief @c socket_entity caster specialization for fabricating it from another net entities
     */
    template<int domain_v, int type_v>
    struct ace::io::caster<ace::net::socket_entity<domain_v, type_v>>
        : net::net_entity_caster<net::socket_entity<domain_v, type_v>> {
        using net::net_entity_caster<net::socket_entity<domain_v, type_v>>::from_entity;
    };


// ================================- DEFINITIONS -================================


    template <int domain_v, ace::net::transport_entity_state connection_state_v>
    struct ace::net::transport_entity : net_entity<transport_entity<domain_v, connection_state_v>> {

        IMPORT_IO_NET_ENTITY_ENV(transport_entity)
        IMPORT_IO_NET_ENTITY_FABRICATION

        /** @brief Default constructor. */
        transport_entity() = default;

        /// @brief Query type used to connect this entity to a peer.
        using connect_query_t = connect_query<transport_entity, domain_v>;
        friend connect_query_t;

        /**
         * @brief Awaitable query for sending a datagram to a specific address.
         * @details A length above @c kernel_controller::max_io_length
         * completes without submission and returns @c -EOVERFLOW.
         */
        struct sendto_query : io::query<sendto_query> {

            IMPORT_IO_QUERY_ENV(sendto_query);

            /** @brief Deleted: a sendto query requires a fd, a buffer and an address. */
            sendto_query() = delete;

            /**
             * @brief Constructs a sendto query.
             * @param fd       Socket descriptor.
             * @param buf      Data buffer to send.
             * @param len      Buffer length.
             * @param flags    Send flags.
             * @param addr     Destination address.
             * @param addrlen  Length of @p addr.
             */
            explicit sendto_query(const int fd, const void *buf, const size_t len, const int flags,
                const sockaddr *addr, const socklen_t addrlen)
                : io_query_t(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags)
                , _addr(addr)
                , _addrlen(addrlen) {
                if (not services::kernel_controller::is_io_length_supported(len))
                    this->fail_before_submission(EOVERFLOW);
            }

            /** @brief Submits the sendto operation to the kernel controller. */
            bool setup_query(services::kernel_observer* kwp) const {
                return services::kernel_controller::sendto(kwp, _fd, _buf, _len, _flags, _addr, _addrlen);
            }

            /** @brief Returns the number of bytes sent (or a negative error code). */
            [[nodiscard]] int await_resume() const { return _res; }

            const void *_buf;      ///< Data buffer to send.
            const size_t _len;     ///< Buffer length.
            const int _flags;      ///< Send flags.
            const sockaddr* _addr; ///< Destination address.
            const socklen_t _addrlen; ///< Length of @c _addr.
        };


        /**
         * @brief Awaitable query for scatter-gather send.
         */
        struct sendmsg_query : io::query<sendmsg_query> {

            IMPORT_IO_QUERY_ENV(sendmsg_query)

            /** @brief Deleted: a sendmsg query requires a fd and a message header. */
            sendmsg_query() = delete;

            /**
             * @brief Constructs a sendmsg query.
             * @param fd     Socket descriptor.
             * @param msg    Scatter-gather message header.
             * @param flags  Send flags (default 0).
             */
            explicit sendmsg_query(const int fd, msghdr* msg, const int flags = 0)
                : io_query_t(fd)
                , _msg(msg)
                , _flags(flags) {}

            /** @brief Submits the sendmsg operation to the kernel controller. */
            bool setup_query(services::kernel_observer* kwp) const {
                return services::kernel_controller::sendmsg(kwp, _fd, _msg, _flags);
            }

            /** @brief Returns the number of bytes sent (or a negative error code). */
            [[nodiscard]] int await_resume() const { return _res; }

            msghdr* _msg;       ///< Scatter-gather message header.
            const int _flags;   ///< Send flags.
        };

        /**
         * @brief Awaitable query for scatter-gather receive.
         */
        struct recvmsg_query : io::query<recvmsg_query> {

            IMPORT_IO_QUERY_ENV(recvmsg_query)

            /** @brief Deleted: a recvmsg query requires a fd and a message header. */
            recvmsg_query() = delete;

            /**
             * @brief Constructs a recvmsg query.
             * @param fd     Socket descriptor.
             * @param msg    Scatter-gather message header.
             * @param flags  Receive flags (default 0).
             */
            explicit recvmsg_query(const int fd, msghdr* msg, const int flags = 0)
                : io_query_t(fd)
                , _msg(msg)
                , _flags(flags) {}

            /** @brief Submits the recvmsg operation to the kernel controller. */
            bool setup_query(services::kernel_observer* kwp) const {
                return services::kernel_controller::recvmsg(kwp, _fd, _msg, _flags);
            }

            /** @brief Returns the number of bytes received (or a negative error code). */
            [[nodiscard]] int await_resume() const { return _res; }

            msghdr* _msg;       ///< Scatter-gather message header.
            const int _flags;   ///< Receive flags.
        };

        /** @brief Sends a byte range over the connected socket. */
        [[nodiscard]] auto send(const void *first, const void* last, const int flags = 0) const
        -> send_query requires (connection_state_v == e_connected) {
            const size_t len = static_cast<const std::byte*>(last) - static_cast<const std::byte*>(first);
            return send_query{_fd, first, len, flags};
        }

        /** @brief Sends a string view over the connected socket. */
        [[nodiscard]] auto send(const std::string_view buf, const int flags = 0) const
        -> send_query requires (connection_state_v == e_connected)
        { return send_query{_fd, buf.data(), buf.size(), flags}; }

        /** @brief Sends a POD vector over the connected socket. */
        template <typename data_t>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto send(const std::vector<data_t>& buf, const int flags = 0) const
        -> send_query requires (connection_state_v == e_connected)
        { return send_query{_fd, buf.data(), buf.size() * (sizeof(data_t) / sizeof(char)), flags}; }

        /** @brief Sends a POD array over the connected socket. */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto send(const std::array<data_t, len_v>& buf, const int flags = 0) const
        -> send_query requires (connection_state_v == e_connected)
        { return send_query{_fd, buf.data(), len_v * (sizeof(data_t) / sizeof(char)), flags}; }

        /** @brief Sends a POD span over the connected socket. */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto send(const std::span<data_t, len_v>& buf, const int flags = 0) const
        -> send_query requires (connection_state_v == e_connected)
        { return send_query{_fd, buf.data(), buf.size_bytes(), flags}; }

        /** @brief Sends a scatter-gather message over the connected socket. */
        [[nodiscard]] auto send(msghdr* msg, const int flags = 0) const
        -> sendmsg_query requires (connection_state_v == e_connected)
        { return sendmsg_query{_fd, msg, flags}; }

        /** @brief Sends an @c io::buffer over the connected socket. */
        [[nodiscard]] auto send(io::buffer& buf, const int flags = 0) const
        -> sendmsg_query requires (connection_state_v == e_connected)
        { return sendmsg_query{_fd, buf.assemble(), flags}; }

        /**
         * @brief Connects the entity to a remote address.
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen)
        -> connect_query_t requires (connection_state_v == e_indirect)
        { return connect_query_t{ std::move(*this), addr, addrlen}; }

        /**
         * @brief Connects the entity to an IPv4 address.
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
         * @brief Connects the entity to a string address.
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const std::string_view addr, const uint16_t port)
        -> connect_query_t requires (is_inet_domain<domain_v> and connection_state_v == e_indirect) {
            _peer_sin.sin_family = domain_v;
            _peer_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_peer_sin.sin_addr));
            return connect_query_t { std::move(*this), reinterpret_cast<sockaddr*>(&_peer_sin), sizeof(sockaddr_in)};
        }

        /** @brief Sends a byte range to a specific address. */
        [[nodiscard]] auto sendto(const void *first, const void* last, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect) {
            const size_t len = static_cast<const std::byte*>(last) - static_cast<const std::byte*>(first);
            return sendto_query{_fd, first, len, flags, addr, addrlen};
        }

        /** @brief Sends a string view to a specific address. */
        [[nodiscard]] auto sendto(const std::string_view buf, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect)
        { return sendto_query{_fd, buf.data(), buf.size(), flags, addr, addrlen}; }

        /** @brief Sends a POD vector to a specific address. */
        template <typename data_t>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto sendto(const std::vector<data_t>& buf, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect)
        { return sendto_query{_fd, buf.data(), buf.size() * (sizeof(data_t) / sizeof(char)), flags, addr, addrlen}; }

        /** @brief Sends a string to a specific address. */
        [[nodiscard]] auto sendto(const std::string& buf, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect)
        { return sendto_query{_fd, buf.data(), buf.size(), flags, addr, addrlen}; }

        /** @brief Sends a POD array to a specific address. */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto sendto(const std::array<data_t, len_v>& buf, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect)
        { return sendto_query{_fd, buf.data(), len_v * (sizeof(data_t) / sizeof(char)), flags, addr, addrlen}; }

        /** @brief Sends a POD span to a specific address. */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto sendto(const std::span<data_t, len_v>& buf, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query requires (connection_state_v == e_indirect)
        { return sendto_query{_fd, buf.data(), buf.size_bytes(), flags, addr, addrlen}; }

        /** @brief Receives data into a raw buffer. */
        [[nodiscard]] auto recv(void *buf, const size_t len, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf, len, flags}; }

        /**
         * @brief Receives data into the vector's existing elements.
         * @details Resize @p buf to the desired writable element count before
         * calling this function; unused capacity is never written. Awaiting the
         * returned query yields the number of bytes received and does not resize
         * the vector.
         */
        template <typename data_t>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto recv(std::vector<data_t>& buf, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf.data(), buf.size() * sizeof(data_t), flags}; }

        /**
         * @brief Receives data into the string's existing characters.
         * @details Resize @p buf to the desired writable byte count before
         * calling this function; unused capacity is never written. Awaiting the
         * returned query yields the number of bytes received and does not resize
         * the string.
         */
        [[nodiscard]] auto recv(std::string& buf, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf.data(), buf.size(), flags}; }

        /** @brief Receives data into a POD array. */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto recv(std::array<data_t, len_v>& buf, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf.data(), len_v * (sizeof(data_t) / sizeof(char)), flags}; }

        /** @brief Receives data into a POD span. */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto recv(std::span<data_t, len_v>& buf, const int flags = 0) const
        -> recv_query { return recv_query{_fd, buf.data(), buf.size_bytes(), flags}; }

        /** @brief Receives a scatter-gather message. */
        [[nodiscard]] auto recv(msghdr* msg, const int flags = 0) const
        -> recvmsg_query { return recvmsg_query{_fd, msg, flags}; }

        /** @brief Receives into an @c io::buffer. */
        [[nodiscard]] auto recv(io::buffer& buff, const int flags = 0) const
        -> recvmsg_query { return recvmsg_query{_fd, buff.assemble(), flags}; }

        /**
         * @brief Eagerly reads all available data into a growing @c io::buffer.
         * @return @c io::input_t containing the buffer, or an error code.
         */
        [[nodiscard]] auto recv_buf(const int flags = 0) const
        -> promise<io::input_t> {
            static constexpr int buf_len = 64;

            io::buffer buf {};
            auto data = buf.expand(buf_len);

            int bytes_read = co_await recv_query(_fd, data, buf_len, flags);
            if (bytes_read < 1) co_return std::unexpected(-bytes_read);

            while (bytes_read == buf_len) {
                data = buf.expand(buf_len);
                bytes_read = co_await recv_query(_fd, data, buf_len, flags);
                if (bytes_read < 1) co_return std::unexpected(-bytes_read);
            }

            if (bytes_read < buf_len)
                buf.shape(bytes_read);

            co_return std::forward<io::buffer>(buf);
        }
    };


    /**
     * @brief Listening socket entity — produces accepted connections.
     *
     * @tparam domain_v  Address family.
     */
    template <int domain_v>
    struct ace::net::listener_entity : net_entity<listener_entity<domain_v>> {

        IMPORT_IO_NET_ENTITY_ENV(listener_entity);
        IMPORT_IO_NET_ENTITY_FABRICATION

        /** @brief Default constructor. */
        listener_entity() = default;

        friend io::caster<listener_entity>;

        /**
         * @brief Awaitable query for accepting a new connection.
         */
        struct accept_query : io::query<accept_query> {

            IMPORT_IO_QUERY_ENV(accept_query)

            /** @brief Deleted: an accept query requires a listener entity. */
            accept_query() = delete;

            /// @brief Resulting connected transport entity type.
            typedef transport_entity<domain_v, e_connected> io_transport_entity_t;

            /**
             * @brief Constructs an accept query.
             * @param entity   Listener entity to accept from.
             * @param addr     Storage for the peer address.
             * @param addrlen  In/out length of @p addr.
             * @param flags    Accept flags (default 0).
             */
            explicit accept_query(const listener_entity* entity, sockaddr* addr, socklen_t* addrlen, const int flags = 0)
                : io_query_t(entity->_fd)
                , _entity(entity)
                , _addr(addr)
                , _addrlen(addrlen)
                , _flags(flags) {}

            /** @brief Submits the accept operation to the kernel controller. */
            bool setup_query(services::kernel_observer* kwp) const {
                return services::kernel_controller::accept(kwp, _fd, _addr, _addrlen, _flags);
            }

            /**
             * @brief Returns the accepted connection on success.
             * @return Connected entity, or a failed entity carrying the error code.
             */
            [[nodiscard]] io_transport_entity_t await_resume() const {
                if (_res > -1) {
                    _entity->_peer_sin = *reinterpret_cast<sockaddr_in*>(_addr);
                    return io::caster<io_transport_entity_t>::from_entity(_res, false, std::move(*_entity));
                }
                return io_transport_entity_t {_res, true};
            }

            const listener_entity* _entity;  ///< Listener entity being accepted from.
            sockaddr* _addr;                 ///< Storage for the peer address.
            socklen_t* _addrlen;             ///< In/out length of @c _addr.
            const int _flags;                ///< Accept flags.
        };

        /** @brief Accepts a new connection, filling @c _self_sin with the peer address. */
        [[nodiscard]] auto accept()
        -> accept_query { return accept_query { this, reinterpret_cast<sockaddr*>(&_self_sin), &_self_sin_size}; }

        /** @brief Accepts a new connection into the given address storage. */
        [[nodiscard]] auto accept(sockaddr* addr, const socklen_t* addrlen, const int flags = 0)
        -> accept_query { return accept_query{this, addr, addrlen, flags}; }

        /** @brief Accepts a new connection for a specific IPv4 address and port. */
        [[nodiscard]] auto accept(const in_addr_t addr, const uint16_t port)
        -> accept_query requires is_inet_domain<domain_v> {
            _self_sin.sin_family = domain_v;
            _self_sin.sin_port = htons(port);
            _self_sin.sin_addr.s_addr = htonl(addr);
            return accept_query { this, reinterpret_cast<sockaddr*>(&_self_sin), &_self_sin_size};
        }

        /** @brief Accepts a new connection for a specific string address and port. */
        [[nodiscard]] auto accept(const std::string_view addr, const uint16_t port)
        -> accept_query requires is_inet_domain<domain_v> {
            _self_sin.sin_family = domain_v;
            _self_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_self_sin.sin_addr));
            return accept_query { this, reinterpret_cast<sockaddr*>(&_self_sin), &_self_sin_size};
        }

        socklen_t _self_sin_size = sizeof(sockaddr_in);  ///< Size of @c sockaddr_in used as accept address length.

    };


    /**
     * @brief Bound socket entity — can be connected or turned into a listener.
     *
     * @tparam domain_v  Address family.
     * @tparam type_v    Socket type (SOCK_STREAM / SOCK_DGRAM).
     */
    template <int domain_v, int type_v>
    struct ace::net::stream_mode_entity : net_entity<stream_mode_entity<domain_v, type_v>> {

        IMPORT_IO_NET_ENTITY_ENV(stream_mode_entity)
        IMPORT_IO_NET_ENTITY_FABRICATION

        /** @brief Default constructor. */
        stream_mode_entity() : io_entity_t() {};

        /// @brief Listener entity type produced by @c listen().
        typedef listener_entity<domain_v> io_listener_entity_t;

        /**
         * @brief Awaitable query for turning the stream into a listening socket.
         */
        struct listen_query : io::query<listen_query> {

            IMPORT_IO_QUERY_ENV(listen_query)

            /** @brief Deleted: a listen query requires a stream mode entity. */
            listen_query() = delete;

            /**
             * @brief Constructs a listen query.
             * @param entity   Stream mode entity (consumed on resume).
             * @param backlog  Listen backlog size.
             */
            explicit listen_query(stream_mode_entity&& entity, const int backlog)
                : io_query_t(entity._fd)
                , _entity(entity)
                , _backlog(backlog) {}

            /** @brief Submits the listen operation to the kernel controller. */
            bool setup_query(services::kernel_observer* kwp) const {
                return services::kernel_controller::listen(kwp, _fd, _backlog);
            }

            /** @brief Returns the listener entity on success. */
            [[nodiscard]] io_listener_entity_t await_resume() const {
                return io_listener_entity_t::consume(_entity);
            }

            stream_mode_entity& _entity;  ///< Reference to the source entity being consumed.
            const int _backlog;           ///< Listen backlog size.
        };


        /**
         * @brief Puts the socket into listening mode.
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto listen(const int backlog = 0)
        -> listen_query requires (type_v == SOCK_SEQPACKET or type_v == SOCK_STREAM) {
            return listen_query{ std::move(*this), backlog};
        }

        /// @brief Query type used to connect this entity to a peer.
        using connect_query_t = connect_query<stream_mode_entity, domain_v>;
        friend connect_query_t;

        /**
         * @brief Connects the entity to a remote address.
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen)
        -> connect_query_t { return connect_query_t{ std::move(*this), addr, addrlen}; }

        /**
         * @brief Connects the entity to an IPv4 address.
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
         * @brief Connects the entity to a string address.
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


    /**
     * @brief Unbound socket entity — produced by awaiting a @c socket query.
     *
     * @tparam domain_v  Address family.
     * @tparam type_v    Socket type (SOCK_STREAM / SOCK_DGRAM).
     */
    template <int domain_v, int type_v>
    struct ace::net::socket_entity : net_entity<socket_entity<domain_v, type_v>> {

        IMPORT_IO_NET_ENTITY_ENV(socket_entity)
        IMPORT_IO_NET_ENTITY_FABRICATION

        /** @brief Default constructor. */
        socket_entity() : io_entity_t() {};

        /** @brief Constructs an entity from a raw fd. */
        explicit socket_entity(const int fd) {
            io_entity_t::_fd = fd;
            if (io_entity_t::_fd > -1) io_entity_t::_is_closed = false;
        }

        /**
         * @brief Awaitable query for binding the socket to a local address.
         */
        struct bind_query : io::query<bind_query> {

            IMPORT_IO_QUERY_ENV(bind_query)

            /** @brief Deleted: a bind query requires a socket entity. */
            bind_query() = delete;

            /// @brief Resulting datagram transport entity type (non-stream sockets).
            typedef transport_entity<domain_v, e_indirect> io_transport_entity_t;

            /// @brief Resulting stream mode entity type (SOCK_STREAM sockets).
            typedef stream_mode_entity<domain_v, type_v> io_stream_mode_entity_t;

            /**
             * @brief Constructs a bind query.
             * @param entity   Socket entity (consumed on resume).
             * @param addr     Local address to bind to.
             * @param addrlen  Length of @p addr.
             */
            explicit bind_query(socket_entity&& entity, sockaddr* addr, const socklen_t addrlen)
                : io_query_t(entity._fd)
                , _entity(entity)
                , _addr(addr)
                , _addrlen(addrlen) {}

            /** @brief Submits the bind operation to the kernel controller. */
            bool setup_query(services::kernel_observer* kwp) const {
                return services::kernel_controller::bind(kwp, _fd, _addr, _addrlen);
            }

            /**
             * @brief Returns the stream mode entity (SOCK_STREAM) or the
             *        datagram transport entity otherwise.
             * @details A successful bind consumes the source entity exactly once,
             * preserving its stored local and peer addresses in the result.
             */
            [[nodiscard]] auto await_resume() {
                if constexpr (is_stream_type<type_v>)
                    return io_stream_mode_entity_t::consume(_entity);
                else {
                    if (_res > -1) {
                        _entity._peer_sin = *reinterpret_cast<sockaddr_in*>(_addr);
                        return io_transport_entity_t::consume(_entity);
                    }
                    return io_transport_entity_t {_res, true};
                }
            }

            socket_entity& _entity;     ///< Reference to the source entity being consumed.
            sockaddr* _addr;            ///< Local address to bind to.
            const socklen_t _addrlen;   ///< Length of @c _addr.
        };

        /**
         * @brief Binds the socket to a local address.
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto bind(const sockaddr* addr, const socklen_t addrlen)
        -> bind_query { return bind_query { std::move(*this), addr, addrlen}; }

        /**
         * @brief Binds the socket to an IPv4 address and port.
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
         * @brief Binds the socket to a string address and port.
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto bind(const std::string_view addr, const uint16_t port)
        -> bind_query requires is_inet_domain<domain_v> {
            _self_sin.sin_family = domain_v;
            _self_sin.sin_port = htons(port);
            inet_pton(domain_v, addr.data(), &(_self_sin.sin_addr));
            return bind_query { std::move(*this), reinterpret_cast<sockaddr*>(&_self_sin), sizeof(sockaddr_in)};
        }

        /// @brief Query type used to connect this entity to a peer.
        using connect_query_t = connect_query<socket_entity, domain_v>;
        friend connect_query_t;

        /**
         * @brief Connects the entity to a remote address.
         * @warning This member operation causes @b consumption and will turn entire object into the invalid state
         */
        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen)
        -> connect_query_t { return connect_query_t{ std::move(*this), addr, addrlen}; }

        /**
         * @brief Connects the entity to an IPv4 address.
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
         * @brief Connects the entity to a string address.
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


    /**
     * @brief Awaitable socket factory — creates a socket of the given domain.
     *
     * @details @c co_await resolves to an unbound @c socket_entity.
     *
     * @tparam domain_v    Address family (AF_INET / AF_INET6).
     * @tparam type_v      Socket type (SOCK_STREAM / SOCK_DGRAM / SOCK_RAW).
     * @tparam protocol_v  IP protocol (IPPROTO_TCP / IPPROTO_UDP / IPPROTO_RAW).
     */
    template <int domain_v, int type_v, int protocol_v>
    struct ace::net::socket : io::query<socket<domain_v, type_v, protocol_v>> {

        IMPORT_IO_QUERY_ENV(socket)

        /**
         * @brief Constructs a socket query.
         * @param [in] flags currently unused
         */
        explicit socket(const int flags = 0)
            // NOTE: There is no socket but need supress defaulted '-1' errcode
            : io_query_t(0)
            , _flags(flags) {}

        /** @brief Submits the socket creation operation to the kernel controller. */
        bool setup_query(services::kernel_observer* kwp) const {
            services::kernel_controller::socket(kwp, domain_v, type_v, protocol_v, _flags);
            return true;
        }

        /** @brief Returns the created socket entity on success. */
        [[nodiscard]] socket_entity<domain_v, type_v> await_resume() const {
            return socket_entity<domain_v, type_v>{_res};
        }

        const int _flags;  ///< Socket creation flags (currently unused).
    };


#undef IMPORT_IO_NET_ENTITY_ENV
#undef IMPORT_IO_NET_ENTITY_FABRICATION
#endif //ACE_NET_H
