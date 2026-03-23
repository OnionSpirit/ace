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
     * @brief An interface template to interact with the
     * @b ace::core::kernel_controller through @b co_await operator.
     * <br>Does not define 'activate(...)' and 'resume(...)' logic.
     * @tparam query_t Specific query type.
     */
    template <typename query_t>
    struct base_query_traits : future_traits<query_t>, core::kernel_waiter {

        DECLARE_FUTURE(query_t);
        IMPORT_FUTURE_ENV;

        base_query_traits() {
            static_assert(is_query<query_t>,
                "Query object shall implement 'bool query(ace::core::kernel_waiter*)' method");
        }

        struct io_socket_query_conductor : conductor_handler_t {

            io_socket_query_conductor() = delete;

            explicit io_socket_query_conductor(base_query_traits* query_)
                : _query(query_) {};

            void forward(async<>&& ctx) override {
                _query->_waiter = std::move(ctx);
            }

            void cancel() override {
                core::kernel_controller::cancel(_query, 0);
            }

            ~io_socket_query_conductor() override = default;

            base_query_traits* _query;
        };

        async<> _waiter;
        int32_t _res = -1;

        bool await_ready() override { return false; };

        bool await_suspend(auto coroutine) {
            if (static_cast<query_t*>(this)->query(this)) {
                coroutine.promise()._runner_conductor = io_socket_query_conductor{this};
                return true;
            }
            return false;
        }

        ~base_query_traits() override = default;
    };


    /**
     * @brief An interface template with standard activation to interact with the
     * @b ace::core::kernel_controller through @b co_await operator.
     * <br>Defines common 'activate(...)' and 'resume(...)' logic.
     * @tparam query_t Specific query type.
     */
    template <typename query_t>
    struct complete_query_traits : base_query_traits<query_t> {

        typedef base_query_traits<query_t> future_query_traits_t;
        using future_query_traits_t::_res;
        using future_query_traits_t::_waiter;

        void activate(const int32_t res) override {
            _res = res;
            _waiter.release_future();
            core::runner::reattach(std::move(_waiter));
        }

        int32_t await_resume() const { return _res; }
    };


    struct close_query : complete_query_traits<close_query> {

        close_query() = delete;

        explicit close_query(const int fd)
            : _fd(fd) {}

        bool query(kernel_waiter* kwp) const noexcept {
            if (_fd == -1) return false;
            return core::kernel_controller::close(kwp, _fd);
        }

        int _fd;
    };

    /**
     * @brief RAII socket closer
     */
    struct io_socket_closer final {
        io_socket_closer() = delete;
        explicit io_socket_closer(const int& fd, const bool& closed)
            : _fd(fd)
            , _closed(closed) {}

        const int& _fd;
        const bool& _closed;

        static async<> check_and_close(const bool closed, const int fd) noexcept {
            if (not closed) co_await close_query{fd};
        }

        static void pending_close(const bool closed, const int fd) noexcept {
            schedule(check_and_close(closed, fd));
        }

        ~io_socket_closer() noexcept { pending_close(_closed, _fd); }
    };

    // struct io_socket_connection {
    //
    //     io_socket_connection() = default;
    //
    //     int _fd = -1;              ///< Socket file descriptor
    //     bool _is_closed   = true;  ///< Socket closed flag
    // }

    // struct io_socket_listener {
    //
    //     io_socket_listener() = default;
    //
    //     int _fd = -1;              ///< Socket file descriptor
    //     bool _is_closed   = true;  ///< Socket closed flag
    // }

    struct io_socket_base {

        int _fd = -1;              ///< Socket file descriptor
        bool _is_bind     = false; ///< Socket bind flag
        bool _is_listener = false; ///< Listen socket flag
        bool _is_closed   = true;  ///< Socket closed flag

        io_socket_base() = default;

        explicit io_socket_base(const int fd)
            : _fd(fd) {
            // NOTE: Special constructor on accept
            _is_bind = true;
            _is_closed = false;
        }

        io_socket_base(io_socket_base&& io) noexcept {
            _fd = io._fd;
            _is_bind = io._is_bind;
            _is_listener = io._is_listener;
            _is_closed = io._is_closed;
            io._fd = -1;
            io._is_bind = false;
            io._is_listener = false;
            io._is_closed = true;
        }

        io_socket_base& operator=(io_socket_base&& io)  noexcept {
            _fd = io._fd;
            _is_bind = io._is_bind;
            _is_listener = io._is_listener;
            _is_closed = io._is_closed;
            io._fd = -1;
            io._is_bind = false;
            io._is_listener = false;
            io._is_closed = true;
            return *this;
        }

        struct bind_query : complete_query_traits<bind_query> {

            bind_query() = delete;

            explicit bind_query(const int fd, const sockaddr* addr, const socklen_t addrlen)
                : _fd(fd)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                return core::kernel_controller::bind(kwp, _fd, _addr, _addrlen);
            }

            int _fd;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        struct connect_query : complete_query_traits<connect_query> {

            connect_query() = delete;

            explicit connect_query(const int fd, const sockaddr* addr, const socklen_t addrlen)
                : _fd(fd)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                return core::kernel_controller::connect(kwp, _fd, _addr, _addrlen);
            }

            int _fd;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        struct listen_query : complete_query_traits<listen_query> {

            listen_query() = delete;

            explicit listen_query(const int fd, const int backlog)
                : _fd(fd)
                , _backlog(backlog) {}

            bool query(kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                return core::kernel_controller::listen(kwp, _fd, _backlog);
            }

            int _fd;
            const int _backlog;
        };

        struct accept_query : base_query_traits<accept_query> {

            accept_query() = delete;

            explicit accept_query(const int fd, sockaddr* addr, socklen_t* addrlen, const int flags = 0)
                : _fd(fd)
                , _addr(addr)
                , _addrlen(addrlen)
                , _flags(flags) {}

            bool query(kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                return core::kernel_controller::accept(kwp, _fd, _addr, _addrlen, _flags);
            }

            void activate(const int32_t res) override {
                _res = res;
                _waiter.release_future();
                core::runner::reattach(std::move(_waiter));
            }

            [[nodiscard]] io_socket_base await_resume() const { return io_socket_base{_res}; }

            int _fd;
            sockaddr* _addr;
            socklen_t* _addrlen;
            const int _flags;
        };

        struct send_query : complete_query_traits<send_query> {

            send_query() = delete;

            explicit send_query(const int fd, const void *buf, const size_t len, const int flags = 0)
                : _fd(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags) {}

            bool query(kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                return core::kernel_controller::send(kwp, _fd, _buf, _len, _flags);
            }

            int _fd;
            const void *_buf;
            const size_t _len;
            const int _flags;
        };

        struct sendto_query : complete_query_traits<sendto_query> {

            sendto_query() = delete;

            explicit sendto_query(const int fd, const void *buf, const size_t len, const int flags,
                const sockaddr *addr, const socklen_t addrlen)
                : _fd(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                return core::kernel_controller::sendto(kwp, _fd, _buf, _len, _flags, _addr, _addrlen);
            }

            int _fd;
            const void *_buf;
            const size_t _len;
            const int _flags;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        struct recv_query : complete_query_traits<recv_query> {

            recv_query() = delete;

            explicit recv_query(const int fd, void *buf, const size_t len, const int flags = 0)
                : _fd(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags) {}

            bool query(kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                return core::kernel_controller::recv(kwp, _fd, _buf, _len, _flags);
            }

            int _fd;
            void *_buf;
            const size_t _len;
            const int _flags;
        };

        [[nodiscard]] auto bind(const sockaddr* addr, const socklen_t addrlen) const
        -> bind_query {
            return bind_query {_fd, addr, addrlen};
        }

        [[nodiscard]] auto listen(const int backlog)
        -> listen_query {
            _is_listener = true;
            return listen_query{_fd, backlog};
        }

        [[nodiscard]] auto connect(const sockaddr* addr, const socklen_t addrlen) const
        -> connect_query {
            if (_is_listener) throw std::logic_error{"can not 'connect(...)' at listen socket"};
            return connect_query{_fd, addr, addrlen};
        }

        [[nodiscard]] auto accept(sockaddr* addr, socklen_t* addrlen, const int flags = 0) const
        -> accept_query {
            if (not _is_listener) throw std::logic_error{"can 'accept(...)' only at listen socket"};
            return accept_query{_fd, addr, addrlen, flags};
        }

        [[nodiscard]] auto send(const void *buf, const size_t len, const int flags = 0) const
        -> send_query {
            if (_is_listener) throw std::logic_error{"can not 'send(...)' at listen socket"};
            return send_query{_fd, buf, len, flags};
        }

        [[nodiscard]] auto sendto(const void *buf, const size_t len, const int flags,
                const sockaddr *addr, const socklen_t addrlen) const
        -> sendto_query {
            if (_is_listener) throw std::logic_error{"can not 'sento(...)' at listen socket"};
            return sendto_query{_fd, buf, len, flags, addr, addrlen};
        }

        [[nodiscard]] auto recv(void *buf, const size_t len, const int flags = 0) const
        -> recv_query {
            if (_is_listener) throw std::logic_error{"can not 'recv(...)' at listen socket"};
            return recv_query{_fd, buf, len, flags};
        }

        [[nodiscard]] auto close()
        -> close_query { _is_closed = true; return close_query{_fd}; }

        virtual ~io_socket_base() = default;

        private:

        io_socket_closer _closer{_fd, _is_closed};
    };

    template <int domain_v, int type_v, int protocol_v>
    struct io_socket_traits : io_socket_base {

        struct open_query : base_query_traits<open_query> {

            typedef base_query_traits<open_query> future_query_traits_t;
            using future_query_traits_t::_res;
            using future_query_traits_t::_waiter;

            /**
             * @param [in, out] fd reference to socket file descriptor variable
             * @param [in] flags currently unused
             */
            explicit open_query(int& fd, const int flags = 0)
                : _fd(fd)
                , _flags(flags) {}

            bool query(core::kernel_waiter* kwp) const {
                core::kernel_controller::socket(kwp, domain_v, type_v, protocol_v, _flags);
                return true;
            }

            void activate(const int32_t res) override {
                _fd = res;
                _waiter.release_future();
                core::runner::reattach(std::move(_waiter));
            }

            int32_t await_resume() { return _res; }

            int& _fd;
            const int _flags;
        };

        /**
         * @brief Opens socket and acquiring its file descriptor
         * @param [in] flags currently unused
         */
        auto open(const int flags = 0)
        -> open_query { return open_query {_fd, flags}; }

        ~io_socket_traits() override = default;
    };

    struct io_socket : io_socket_base {

        io_socket() = delete;

        explicit io_socket(const int domain, const int type, const int protocol)
            : _domain(domain)
            , _type(type)
            , _protocol(protocol) {}

        const int _domain;
        const int _type;
        const int _protocol;

        struct open_query : base_query_traits<open_query> {

            typedef base_query_traits future_query_traits_t;
            using future_query_traits_t::_res;
            using future_query_traits_t::_waiter;

            explicit open_query(int& fd, const int domain, const int type, const int protocol, const int flags = 0)
                : _fd(fd)
                , _domain(domain)
                , _type(type)
                , _protocol(protocol)
                , _flags(flags) {}

            bool query(kernel_waiter* kwp) const {
                core::kernel_controller::socket(kwp, _domain, _type, _protocol, _flags);
                return true;
            }

            void activate(const int32_t res) override {
                _fd = res;
                _waiter.release_future();
                core::runner::reattach(std::move(_waiter));
            }

            int32_t await_resume() { return _res; }

            int& _fd;
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

    using io_socket_raw    = io_socket_traits<AF_INET, SOCK_RAW, IPPROTO_RAW>;
    using io_socket_tcp    = io_socket_traits<AF_INET, SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_tcp_v6 = io_socket_traits<AF_INET6, SOCK_STREAM, IPPROTO_TCP>;
    using io_socket_udp    = io_socket_traits<AF_INET, SOCK_DGRAM, IPPROTO_UDP>;
    using io_socket_udp_v6 = io_socket_traits<AF_INET6, SOCK_DGRAM, IPPROTO_UDP>;

} // end namespace ace::futures

#endif //ACE_NET_H
