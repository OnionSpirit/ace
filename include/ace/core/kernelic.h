#ifndef ACE_CORE_KERNELIC_H
#define ACE_CORE_KERNELIC_H

#include <cstring>
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
            if (io_uring* ring; _controllers.pop(ring))
                io_uring_submit(ring);
            return not _controllers.empty();
        }

        static nukes::dynamic::mpsc_queue<io_uring*> _controllers;

        static void request_submission(io_uring* ring) {
            while (not _controllers.push(std::move(ring))) {};
            touch();
        }
    };

    /**
     * @brief Uring user_data proxy entity with activation method
     */
    struct kernel_waiter {

        kernel_waiter() = default;

        explicit kernel_waiter(runner_pool_t* identity)
            : _runner_identity(identity) {}

        /**
         * @brief Activates waiter with CQE result
         * @param res CQE result value
         */
        virtual void activate(int32_t res) = 0;

        bool _on_cancel = false; ///< Next response will indicate count of canceled operations
        bool _multishot = false; ///< Mark if multishot is enabled

        runner_pool_t* _runner_identity = nullptr;

        virtual ~kernel_waiter() = default;
    };

    /**
     * @brief Abstract object to interact with kernel controller
     */
    struct kernel_entity {

        template <typename io_uring_foo_t, typename ... Args>
        kernel_entity(io_uring_foo_t foo, io_uring_sqe *sqe, Args... args) {
            _action = action_templ<io_uring_foo_t, Args...>;
            _sqe = sqe;
            _io_uring_foo = reinterpret_cast<void*>(foo);
            // NOTE: Placement new to copy params to the local storage
            new (_params) std::tuple<Args...>(args...);
        }

        // NOTE: Polymorphic action handler
        template <typename io_uring_foo_t, typename ... Args>
        static void action_templ(void* io_uring_foo, io_uring_sqe* sqe, uintptr_t* params) {
            std::tuple<Args...> tuple { *reinterpret_cast<std::tuple<Args...>*>(params) };
            io_uring_foo_t foo { reinterpret_cast<io_uring_foo_t>(io_uring_foo) };
            [&]<std::size_t ... index_v>(std::index_sequence<index_v...>) {
                foo(sqe, std::get<index_v>(tuple)...);
            }(std::make_index_sequence<sizeof...(Args)>{});
        }

        void apply() {
            if (_sqe not_eq nullptr)
                _action(_io_uring_foo, _sqe, _params);
        }

        ACE_CACHE_LINE(0)

        uintptr_t _params[8] = {};

        ACE_CACHE_LINE(1)

        void (*_action)(void*, io_uring_sqe*, uintptr_t*) = nullptr;
        io_uring_sqe* _sqe = nullptr;
        void* _io_uring_foo = nullptr;

        static thread_local common::slab_mempool<kernel_entity> _kernelic_entity_mempool;
    };


    /**
     * @brief Thread local vortex to work with uring queues without kernel notification
     */
    struct kernel_controller : vortex_traits<kernel_controller, vortex_spawn_mode::e_thread_local> {

    private:

        static thread_local io_uring_params _ring_params;
        static thread_local io_uring _ring;
        static thread_local int _requests;

    public:

        static constexpr unsigned max_entries = 1024;

        kernel_controller() {
            memset(&_ring_params, 0, sizeof(_ring_params));
            io_uring_queue_init_params(max_entries, &_ring, &_ring_params);
        }

        static bool ping() {
            // NOTE: Setting requests to the io_uring
            const bool need_submission = not _submission_buffer.empty();
            for (int i = 0; i < max_entries and not _submission_buffer.empty(); ++i) {
                auto entity = _submission_buffer.dequeue();
                entity.apply();
            }

            // NOTE: Requesting submission if it's needed
            if (need_submission)
                kernel_notifier::request_submission(&_ring);

            // NOTE: Receiving responses from the io_uring
            io_uring_cqe* cqe_s[max_entries] {};
            const unsigned int cqe_count = io_uring_peek_batch_cqe(&_ring, cqe_s, max_entries);
            for (int i = 0; i < cqe_count; ++i) {

                const auto cqe = cqe_s[i];
                const auto identity = io_uring_cqe_get_data64(cqe);
                const auto waiter = reinterpret_cast<kernel_waiter*>(identity);

                if (waiter->_on_cancel) [[unlikely]] _requests -= cqe->res;
                else [[likely]] waiter->activate(cqe->res);

                io_uring_cqe_seen(&_ring, cqe);
                if (not waiter->_multishot) --_requests;
            }
            return _requests not_eq 0;
        }

        static thread_local common::queue<kernel_entity> _submission_buffer;

        ~kernel_controller() { io_uring_queue_exit(&_ring); }

        /**
         * @brief Submits IO request to controller
         * @warning IO function params shall be passed without SQE ptr
         */
        template <typename foo_t, typename ... Args>
        static void submit(foo_t io_uring_foo, kernel_waiter* waiter, Args... args) {
            io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
            io_uring_sqe_set_data(sqe, waiter);
            ++_requests;
            const auto entity = kernel_entity(io_uring_foo, sqe, args...);
            _submission_buffer.enqueue(entity);
            touch(waiter->_runner_identity);
        }

        static void nop(kernel_waiter* waiter) {
            submit(io_uring_prep_nop, waiter);
        }

        static void cancel(kernel_waiter* waiter, const int flags) {
            waiter->_on_cancel = true;
            submit(io_uring_prep_cancel, waiter, waiter, flags);
        }

        static void cancel_fd(kernel_waiter* waiter, const int fd, const int flags) {
            waiter->_on_cancel = true;
            submit(io_uring_prep_cancel_fd, waiter, fd, flags);
        }

        static void open(kernel_waiter* waiter, const char* path, const int flags, const mode_t mode) {
            submit(io_uring_prep_open, waiter, path, flags, mode);
        }

        static void close(kernel_waiter* waiter, const int fd) {
            submit(io_uring_prep_close, waiter, fd);
        }

        static void bind(kernel_waiter* waiter, const int fd, const sockaddr *addr, const socklen_t addrlen) {
            submit(io_uring_prep_bind, waiter, fd, addr, addrlen);
        }

        static void connect(kernel_waiter* waiter, const int fd, const sockaddr *addr, const socklen_t addrlen) {
            submit(io_uring_prep_connect, waiter, fd, addr, addrlen);
        }

        static void listen(kernel_waiter* waiter, const int fd, const int backlog) {
            submit(io_uring_prep_listen, waiter, fd, backlog);
        }

        static void accept(kernel_waiter* waiter, const int fd, sockaddr *addr, socklen_t *addrlen, const int flags) {
            submit(io_uring_prep_accept, waiter, fd, addr, addrlen, flags);
        }

        static void send(kernel_waiter* waiter, const int fd, const void *buf, const size_t len, const int flags,
                  const sockaddr *addr, const socklen_t addrlen) {
            submit(io_uring_prep_sendto, waiter, fd, buf, len, flags, addr, addrlen);
        }

        static void recv(kernel_waiter* waiter, const int fd, void *buf, const size_t len, const int flags) {
            submit(io_uring_prep_recv, waiter, fd, buf, len, flags);
        }

    };

    nukes::dynamic::mpsc_queue<io_uring*> kernel_notifier::_controllers {};

    thread_local common::slab_mempool<kernel_entity> kernel_entity::_kernelic_entity_mempool {
        common::slab_mempool<kernel_entity>()
    };

    thread_local common::queue<kernel_entity> kernel_controller::_submission_buffer {
        kernel_entity::_kernelic_entity_mempool
    };

    thread_local io_uring_params kernel_controller::_ring_params {};
    thread_local io_uring kernel_controller::_ring {};
    thread_local int kernel_controller::_requests {};

}

#endif //ACE_CORE_KERNELIC_H
