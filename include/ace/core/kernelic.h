#ifndef ACE_CORE_KERNELIC_H
#define ACE_CORE_KERNELIC_H

#include <cstring>
#include <functional>
#include <liburing.h>

#include "vortex.h"
#include "ace/common/queue.h"
#include "ace/common/selection.h"

namespace ace::core {

    /**
     * @brief Shared vortex to notify kernel about uring operations
     */
    struct kernel_notifier : vortex_traits<kernel_notifier, vortex_spawn_mode::e_thread_shared> {

        kernel_notifier() = default;

        static bool ping() {

            return false;
        }

        // static nukes::dynamic::mpsc_queue<>
    };

    /**
     * @brief Uring user_data entity with activation method
     */
    struct kernel_waiter {

        kernel_waiter() = default;

        /**
         * @brief Activates waiter with CQE result
         * @param res CQE result value
         */
        virtual void activate(int32_t res) = 0;

        virtual ~kernel_waiter() = default;
    };

    /**
     * @brief Abstract object to interact with kernel controller
     */
    struct kernel_entity {

        template <typename io_uring_foo_t, typename ... Args>
        kernel_entity(io_uring_foo_t, io_uring_sqe *sqe, Args... args) {
            _action = action_templ<io_uring_foo_t, Args...>;
            _sqe = sqe;
            // NOTE: Placement new to copy params to the local storage
            new (_params) std::tuple<Args...>(args...);
        }

        template <typename io_uring_foo_t, typename ... Args>
        static void action_templ(io_uring_sqe* sqe, uintptr_t* params) {
            std::tuple<Args...> tuple { *reinterpret_cast<std::tuple<Args...>*>(params) };
            io_uring_foo_t f {};
            f(sqe, std::get<Args>(tuple)...);
        }

        void apply() {
            if (_sqe not_eq nullptr)
                _action(_sqe, _params);
        }

        ACE_CACHE_LINE(0)

        uintptr_t _params[8] = {};

        ACE_CACHE_LINE(1)

        void (*_action)(io_uring_sqe*, uintptr_t*) = nullptr;
        io_uring_sqe* _sqe = nullptr;

        static thread_local common::slab_mempool<kernel_entity> _kernelic_entity_mempool;
    };

    /**
     * @brief Thread local vortex to work with uring queues without kernel notification
     */
    struct kernel_controller : vortex_traits<kernel_controller, vortex_spawn_mode::e_thread_local> {

        static constexpr unsigned max_entries = 1024;

        kernel_controller() {
            memset(&_ring_params, 0, sizeof(_ring_params));
            io_uring_queue_init_params(max_entries, &_ring, &_ring_params);
        }

        bool ping() {

            // NOTE: Setting request to the io_uring
            for (int i = 0; i < max_entries and not _submission_buffer.empty(); ++i) {
                auto entity = _submission_buffer.dequeue();
                entity.apply();
            }

            // NOTE: Receiving responses from the io_uring
            io_uring_cqe *cqe_s[max_entries];
            const int cqe_count = io_uring_peek_batch_cqe(&_ring, cqe_s, max_entries);
            for (int i = 0; i < cqe_count; ++i) {
                const auto identity = io_uring_cqe_get_data64(cqe_s[i]);
                const auto waiter = reinterpret_cast<kernel_waiter*>(identity);
                waiter->activate(cqe_s[i]->res);
                io_uring_cqe_seen(&_ring, cqe_s[i]);
                --_waiters;
            }
            // io_uring_submit(&_ring);
            return _waiters not_eq 0;
        }

        common::queue<kernel_entity> _submission_buffer {kernel_entity::_kernelic_entity_mempool};

        ~kernel_controller() { io_uring_queue_exit(&_ring); }

        /**
         * @brief Submits IO request to controller
         * @warning IO function params shall be passed without SQE ptr
         */
        template <typename foo_t, typename ... Args>
        void submit(foo_t io_uring_foo, kernel_waiter* waiter, Args... args) {
            io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
            io_uring_sqe_set_data(sqe, waiter);
            ++_waiters;
            const auto entity = kernel_entity(io_uring_foo, sqe, args...);
            _submission_buffer.enqueue(entity);
        }

        void nop(kernel_waiter* waiter) {
            submit(io_uring_prep_nop, waiter);
        }

        // TODO: Figure out how to control waiters count
        // void cancel(kernel_waiter* waiter, const int flags) {
        //     submit(waiter, io_uring_prep_cancel, waiter, flags);
        // }

        void open(kernel_waiter* waiter, const char* path, const int flags, const mode_t mode) {
            submit(io_uring_prep_open, waiter, path, flags, mode);
        }

        void close(kernel_waiter* waiter, const int fd) {
            submit(io_uring_prep_close, waiter, fd);
        }

        void bind(kernel_waiter* waiter, const int fd, const sockaddr *addr, const socklen_t addrlen) {
            submit(io_uring_prep_bind, waiter, fd, addr, addrlen);
        }

        void connect(kernel_waiter* waiter, const int fd, const sockaddr *addr, const socklen_t addrlen) {
            submit(io_uring_prep_connect, waiter, fd, addr, addrlen);
        }

        void listen(kernel_waiter* waiter, const int fd, const int backlog) {
            submit(io_uring_prep_listen, waiter, fd, backlog);
        }

        void accept(kernel_waiter* waiter, const int fd, sockaddr *addr, socklen_t *addrlen, const int flags) {
            submit(io_uring_prep_accept, waiter, fd, addr, addrlen, flags);
        }

        void send(kernel_waiter* waiter, const int fd, const void *buf, const size_t len, const int flags,
                  const sockaddr *addr, const socklen_t addrlen) {
            submit(io_uring_prep_sendto, waiter, fd, buf, len, flags, addr, addrlen);
        }

        void recv(kernel_waiter* waiter, const int fd, void *buf, const size_t len, const int flags) {
            submit(io_uring_prep_recv, waiter, fd, buf, len, flags);
        }

    private:

        io_uring_params _ring_params;
        io_uring _ring;

        int _waiters = 0;
    };

    thread_local common::slab_mempool<kernel_entity> kernel_entity::_kernelic_entity_mempool =
        common::slab_mempool<kernel_entity>();
}

#endif //ACE_CORE_KERNELIC_H
