#ifndef ACE_NET_H
#define ACE_NET_H

#include "future.h"
#include "ace/core/kernelic.h"

namespace ace::futures {

    struct io_socket {

        int _fd = -1; ///< Socket file descriptor

        // static_assert(sizeof(io_socket_traits) == 1, "Class must have only methods not fields");

        /**
         * @brief An interface to subscribe on the query through @b co_await operator
         * @tparam query_t Specific query type
         */
        template <typename query_t>
        struct future_query_traits : future_traits<future_query_traits<query_t>> {

            DECLARE_FUTURE(future_query_traits);
            IMPORT_FUTURE_ENV;

            // static_assert(requires(query_t q, kernel_waiter* kwp) { { q.query(kwp) } -> std::same_as<bool>; },
            //     "Query object shall implement 'void query(ace::core::kernel_waiter*)' method");

            struct io_socket_query_conductor : conductor_handler_t {

                io_socket_query_conductor() = delete;

                explicit io_socket_query_conductor(future_query_traits* query_)
                    : _query(query_) {};

                void forward(async<>&& ctx) override {
                    _query->_waiter = std::move(ctx);
                }

                void cancel() override {
                    core::kernel_controller::cancel(_query, 0);
                }

                ~io_socket_query_conductor() override = default;

                future_query_traits* _query;
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

            int32_t await_resume() const { return _res; }

            ~future_query_traits() override = default;
        };

        /**
         * @brief An interface to interact with the
         * @b ace::core::kernel_controller through @b co_await operator
         * @tparam query_t Specific query type
         */
        template <typename query_t>
        struct io_socket_query_traits : future_query_traits<query_t>, core::kernel_waiter {

            typedef future_query_traits<query_t> future_query_traits_t;
            using future_query_traits_t::_res;
            using future_query_traits_t::_waiter;

            void activate(const int32_t res) override {
                _res = res;
                _waiter.release_future();
                core::runner::reattach(std::move(_waiter));
            }
        };

        struct close_query : io_socket_query_traits<close_query> {

            close_query() = delete;

            explicit close_query(const int fd)
                : _fd(fd) {}

            bool query(core::kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                core::kernel_controller::close(kwp, _fd);
                return true;
            }

            int _fd;
        };

        struct bind_query : io_socket_query_traits<bind_query> {

            bind_query() = delete;

            explicit bind_query(const int fd, const sockaddr* addr, const socklen_t addrlen)
                : _fd(fd)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(core::kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                core::kernel_controller::bind(kwp, _fd, _addr, _addrlen);
                return true;
            }

            int _fd;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        struct connect_query : io_socket_query_traits<connect_query> {

            connect_query() = delete;

            explicit connect_query(const int fd, const sockaddr* addr, const socklen_t addrlen)
                : _fd(fd)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(core::kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                core::kernel_controller::connect(kwp, _fd, _addr, _addrlen);
                return true;
            }

            int _fd;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        struct listen_query : io_socket_query_traits<listen_query> {

            listen_query() = delete;

            explicit listen_query(const int fd, const int backlog)
                : _fd(fd)
                , _backlog(backlog) {}

            bool query(core::kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                core::kernel_controller::listen(kwp, _fd, _backlog);
                return true;
            }

            int _fd;
            const int _backlog;
        };

        struct accept_query : io_socket_query_traits<accept_query> {

            accept_query() = delete;

            explicit accept_query(const int fd, sockaddr* addr, socklen_t* addrlen, const int flags = 0)
                : _fd(fd)
                , _addr(addr)
                , _addrlen(addrlen)
                , _flags(flags) {}

            bool query(core::kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                core::kernel_controller::accept(kwp, _fd, _addr, _addrlen, _flags);
                return true;
            }

            int _fd;
            sockaddr* _addr;
            socklen_t* _addrlen;
            const int _flags;
        };

        struct sendto_query : io_socket_query_traits<sendto_query> {

            sendto_query() = delete;

            explicit sendto_query(const int fd, const void *buf, const size_t len, const int flags,
                const sockaddr *addr, const socklen_t addrlen)
                : _fd(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags)
                , _addr(addr)
                , _addrlen(addrlen) {}

            bool query(core::kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                core::kernel_controller::sendto(kwp, _fd, _buf, _len, _flags, _addr, _addrlen);
                return true;
            }

            int _fd;
            const void *_buf;
            const size_t _len;
            const int _flags;
            const sockaddr* _addr;
            const socklen_t _addrlen;
        };

        struct recv_query : io_socket_query_traits<recv_query> {

            recv_query() = delete;

            explicit recv_query(const int fd, void *buf, const size_t len, const int flags = 0)
                : _fd(fd)
                , _buf(buf)
                , _len(len)
                , _flags(flags) {}

            bool query(core::kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                core::kernel_controller::recv(kwp, _fd, _buf, _len, _flags);
                return true;
            }

            int _fd;
            void *_buf;
            const size_t _len;
            const int _flags;
        };

        struct cancel_fd_query : io_socket_query_traits<cancel_fd_query> {

            cancel_fd_query() = delete;

            explicit cancel_fd_query(const int fd, const int flags = 0)
                : _fd(fd)
                , _flags(flags) {}

            bool query(core::kernel_waiter* kwp) const {
                if (_fd == -1) return false;
                core::kernel_controller::cancel_fd(kwp, _fd, _flags);
                return true;
            }

            int _fd;
            const int _flags;
        };

        virtual ~io_socket() = default;
    };

    template <int domain_v, int type_v, int protocol_v>
    struct io_socket_traits : io_socket {

        struct open_query : future_query_traits<open_query>, core::kernel_waiter {

            typedef future_query_traits<open_query> future_query_traits_t;
            using future_query_traits_t::_res;
            using future_query_traits_t::_waiter;

            /**
             * @param [in, out] fd reference to socket file descriptor variable
             * @param [in] flags IO operation flags
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

            int& _fd;
            const int _flags;
        };

        ~io_socket_traits() override = default;
    };

    struct io_socket_custom : io_socket {

        struct open_query : future_query_traits<open_query>, core::kernel_waiter {

            typedef future_query_traits future_query_traits_t;
            using future_query_traits_t::_res;
            using future_query_traits_t::_waiter;

            explicit open_query(int& fd, const int domain, const int type, const int protocol, const int flags = 0)
                : _fd(fd)
                , _domain(domain)
                , _type(type)
                , _protocol(protocol)
                , _flags(flags) {}

            bool query(core::kernel_waiter* kwp) const {
                core::kernel_controller::socket(kwp, _domain, _type, _protocol, _flags);
                return true;
            }

            void activate(const int32_t res) override {
                _fd = res;
                _waiter.release_future();
                core::runner::reattach(std::move(_waiter));
            }

            int& _fd;
            const int _domain;
            const int _type;
            const int _protocol;
            const int _flags;
        };

        ~io_socket_custom() override = default;
    };

} // end namespace ace::futures

#endif //ACE_NET_H
