#ifndef ACE_CORE_KERNELIC_H
#define ACE_CORE_KERNELIC_H

#include <algorithm>
#include <cstring>
#include <liburing.h>

#include "ace/core/traits/vortex.h"
#include "ace/core/misc/queue.h"
#include "ace/core/misc/selection.h"

namespace ace::core {

    /**
     * @brief Proxy entity with activation method to use it as uring user_data
     */
    struct kernel_observer {

        kernel_observer() = default;

        explicit kernel_observer(runner_pool_t* identity)
            : _runner_identity(identity) {}

        /**
         * @brief Activates waiter with CQE result
         * @param res CQE result value
         */
        virtual void on_result(int res) = 0;

        bool _on_cancel = false; ///< Next response will indicate count of canceled operations
        bool _multishot = false; ///< Mark if multishot is enabled

        runner_pool_t* _runner_identity = nullptr;

        virtual ~kernel_observer() = default;
    };


    /**
     * @brief Thread local vortex to work with uring queues without kernel notification
     */
    struct kernel_controller : vortex_traits<kernel_controller, vortex_spawn_mode::e_thread_local> {

    private:

        struct kernel_entity;

        static thread_local io_uring_params _ring_params;
        static thread_local io_uring _ring;
        static thread_local int _queries;

    public:

        kernel_controller();

        ~kernel_controller();

        static constexpr unsigned max_entries = 4096;

        static thread_local ace::misc::queue<kernel_entity> _submission_buffer;

        static bool ping();

        /**
         * @brief Submits IO request to controller.
         * @param [in] io_uring_foo IO function ptr.
         * @param [in] waiter pointer to an external object that waits
         * for the operation result of the requested operation.
         * @param [in, out] params... IO function params (without sqe).
         * @warning IO function params shall be passed without SQE ptr.
         *
         * Same order but waiter ptr required instead of the SQE ptr:
         *
         * @c io_uring_prep_open(sqe, path, flags, mode) - @b liburing @b interface.
         *
         * @c submit(io_uring_prep_open, waiter, path, flags, mode) - @b submit @b interface.
         */
        template <typename foo_t, typename ... Params>
        static bool submit(foo_t io_uring_foo, kernel_observer* waiter, Params... params) noexcept;

        static bool nop(kernel_observer* waiter) {
            return submit(io_uring_prep_nop, waiter);
        }

        static bool socket(kernel_observer* waiter, const int domain, const int type,
        const int protocol, const unsigned int flags) {
            return submit(io_uring_prep_socket, waiter, domain, type, protocol, flags);
        }

        static bool cancel(kernel_observer* waiter, const int flags) {
            return waiter->_on_cancel = submit(io_uring_prep_cancel, waiter, waiter, flags);
        }

        static bool cancel_fd(kernel_observer* waiter, const int fd, const int flags) {
            return waiter->_on_cancel = submit(io_uring_prep_cancel_fd, waiter, fd, flags);
        }

        static bool open(kernel_observer* waiter, const char* path, const int flags, const mode_t mode) {
            return submit(io_uring_prep_open, waiter, path, flags, mode);
        }

        static bool close(kernel_observer* waiter, const int fd) noexcept {
            return submit(io_uring_prep_close, waiter, fd);
        }

        static bool bind(kernel_observer* waiter, const int fd, const sockaddr *addr, const socklen_t addrlen) {
            return submit(io_uring_prep_bind, waiter, fd, addr, addrlen);
        }

        static bool connect(kernel_observer* waiter, const int fd, const sockaddr *addr, const socklen_t addrlen) {
            return submit(io_uring_prep_connect, waiter, fd, addr, addrlen);
        }

        static bool listen(kernel_observer* waiter, const int fd, const int backlog) {
            return submit(io_uring_prep_listen, waiter, fd, backlog);
        }

        static bool accept(kernel_observer* waiter, const int fd, sockaddr *addr, socklen_t *addrlen, const int flags) {
            return submit(io_uring_prep_accept, waiter, fd, addr, addrlen, flags);
        }

        static bool send(kernel_observer* waiter, const int fd, const void *buf, const size_t len, const int flags) {
            return submit(io_uring_prep_send, waiter, fd, buf, len, flags);
        }

        static bool sendto(kernel_observer* waiter, const int fd, const void *buf, const size_t len, const int flags,
        const sockaddr *addr, const socklen_t addrlen) {
            return submit(io_uring_prep_sendto, waiter, fd, buf, len, flags, addr, addrlen);
        }

        static bool recv(kernel_observer* waiter, const int fd, void *buf, const size_t len, const int flags) {
            return submit(io_uring_prep_recv, waiter, fd, buf, len, flags);
        }

        static bool read(kernel_observer* waiter, const int fd, void *buf, const unsigned nbytes, const uint64_t offset) {
            return submit(io_uring_prep_read, waiter, fd, buf, nbytes, offset);
        }

        static bool write(kernel_observer* waiter, const int fd, const void *buf, const unsigned nbytes, const uint64_t offset) {
            return submit(io_uring_prep_write, waiter, fd, buf, nbytes, offset);
        }

    };

    /**
     * @brief Abstract object to interact with kernel controller
     */
    struct kernel_controller::kernel_entity {

        template <typename io_uring_foo_t, typename ... Args>
        kernel_entity(io_uring_foo_t foo, io_uring_sqe *sqe, Args... args);

        // NOTE: Polymorphic action handler
        template <typename io_uring_foo_t, typename ... Args>
        static void action_templ(void* io_uring_foo, io_uring_sqe* sqe, const uintptr_t* params);

        void apply();

        ACE_CACHE_LINE(0)

        uintptr_t _params[8] = {};

        ACE_CACHE_LINE(1)

        void (*_action)(void*, io_uring_sqe*, const uintptr_t*) = nullptr;
        io_uring_sqe* _sqe = nullptr;
        void* _io_uring_foo = nullptr;

        static thread_local ace::misc::slab_mempool<kernel_entity> _kernelic_entity_mempool;
    };

    thread_local ace::misc::slab_mempool<kernel_controller::kernel_entity> kernel_controller::kernel_entity::_kernelic_entity_mempool {
        ace::misc::slab_mempool<kernel_entity>()
    };

    thread_local ace::misc::queue<kernel_controller::kernel_entity> kernel_controller::_submission_buffer {
        kernel_entity::_kernelic_entity_mempool
    };

    thread_local io_uring_params kernel_controller::_ring_params {};
    thread_local io_uring kernel_controller::_ring {};
    thread_local int kernel_controller::_queries {};

}

#define ACE_CORE_KERNEL_CONTROLLER_SPACE \
ace::core::kernel_controller::

#define ACE_CORE_KERNEL_CONTROLLER_MEMBER(returnT) \
inline returnT ACE_CORE_KERNEL_CONTROLLER_SPACE

#define ACE_CORE_KERNEL_ENTITY_SPACE \
ace::core::kernel_controller::kernel_entity::

#define ACE_CORE_KERNEL_ENTITY_MEMBER(returnT) \
inline returnT ACE_CORE_KERNEL_ENTITY_SPACE


ACE_CORE_KERNEL_CONTROLLER_MEMBER()
kernel_controller() {
    memset(&_ring_params, 0, sizeof(_ring_params));
    io_uring_queue_init_params(max_entries, &_ring, &_ring_params);
}


ACE_CORE_KERNEL_CONTROLLER_MEMBER()
~kernel_controller() { io_uring_queue_exit(&_ring); }


ACE_CORE_KERNEL_CONTROLLER_MEMBER(bool)
ping() {
    // NOTE: Setting requests to the io_uring
    const bool need_submission = not _submission_buffer.empty();
    for (unsigned int i = 0; i < max_entries and not _submission_buffer.empty(); ++i) {
        auto entity = _submission_buffer.dequeue();
        entity.apply();
    }

    // NOTE: Requesting submission if it's needed
    if (need_submission)
        io_uring_submit(&_ring);

    // NOTE: Receiving responses from the io_uring
    io_uring_cqe* cqe_s[max_entries] {};
    const unsigned int cqe_count = io_uring_peek_batch_cqe(&_ring, cqe_s, max_entries);
    for (unsigned int i = 0; i < cqe_count; ++i) {

        const auto cqe = cqe_s[i];
        const auto identity = io_uring_cqe_get_data64(cqe);
        const auto waiter = reinterpret_cast<kernel_observer*>(identity);

        if (waiter == nullptr)
            continue;

        waiter->on_result(cqe->res);

        if (not waiter->_multishot)
            --_queries;
        else if (waiter->_multishot and waiter->_on_cancel)
            _queries -= cqe->res;

        io_uring_cqe_seen(&_ring, cqe);
    }

    return _queries not_eq 0;
}


template <typename foo_t, typename ... Params> bool
ACE_CORE_KERNEL_CONTROLLER_SPACE
submit(foo_t io_uring_foo, kernel_observer* waiter, Params... params) noexcept {
    touch(waiter->_runner_identity);
    io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
    io_uring_sqe_set_data(sqe, waiter);
    ++_queries;
    if (not _submission_buffer.enqueue( kernel_entity{io_uring_foo, sqe, params...} )) [[unlikely]] {
        sqe = io_uring_get_sqe(&_ring);
        io_uring_sqe_set_data(sqe, nullptr);
        io_uring_prep_cancel(sqe, waiter, 0);
        --_queries;
        return false;
    }
    return true;
}


template <typename io_uring_foo_t, typename ... Args>
ACE_CORE_KERNEL_ENTITY_SPACE
kernel_entity(io_uring_foo_t foo, io_uring_sqe *sqe, Args... args) {
    _action = action_templ<io_uring_foo_t, Args...>;
    _sqe = sqe;
    _io_uring_foo = reinterpret_cast<void*>(foo);
    // NOTE: Placement new to copy params to the local storage
    new (_params) std::tuple<Args...>(args...);
}


template <typename io_uring_foo_t, typename ... Args> void
ACE_CORE_KERNEL_ENTITY_SPACE
action_templ(void* io_uring_foo, io_uring_sqe* sqe, const uintptr_t* params) {
    std::tuple<Args...> tuple { *reinterpret_cast<const std::tuple<Args...>*>(params) };
    io_uring_foo_t foo { reinterpret_cast<io_uring_foo_t>(io_uring_foo) };
    [&]<std::size_t ... index_v>(std::index_sequence<index_v...>) {
        foo(sqe, std::get<index_v>(tuple)...);
    }(std::make_index_sequence<sizeof...(Args)>{});
}


ACE_CORE_KERNEL_ENTITY_MEMBER(void)
apply() {
    if (_sqe not_eq nullptr)
        _action(_io_uring_foo, _sqe, _params);
}

#undef ACE_CORE_KERNEL_CONTROLLER_SPACE
#undef ACE_CORE_KERNEL_CONTROLLER_MEMBER
#undef ACE_CORE_KERNEL_ENTITY_SPACE
#undef ACE_CORE_KERNEL_ENTITY_MEMBER
#endif //ACE_CORE_KERNELIC_H
