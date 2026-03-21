#ifndef ACE_CORE_KERNELIC_H
#define ACE_CORE_KERNELIC_H

#include <cstring>
#include <liburing.h>
#include <unordered_map>

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
    };

    /**
     * @brief Supported operation types via kernelic controller
     */
    enum class operation_type {
        e_nop,
        e_open,
        e_close,
        e_read,
        e_write,
        e_accept,
        e_connect,
        e_send,
        e_recv,
    };

    struct operation_mapping {
        operation_type type;
        void (*submission_function)();
        void (*completion_function)();
    };

    // static auto operation_mappings[][4] = {
    //     {}
    // };

    /**
     * @brief Abstract object to interact with kernel controller
     */
    struct kernel_entity {

        operation_type _op_code;  ///< Operation type to do
        int32_t _res;             ///< Operation completion result
        async<> _user;            ///< User of request. Actually [s|c]qe user_data

        static thread_local common::slab_mempool<kernel_entity> _kernelic_entity_mempool;
    };

    /**
     * @brief Thread local vortex to work with uring queues without kernel notification
     */
    struct kernel_controller : vortex_traits<kernel_controller, vortex_spawn_mode::e_thread_local> {

        static constexpr unsigned max_entries = 128;

        kernel_controller() {
            memset(&_ring_params, 0, sizeof(_ring_params));
            io_uring_queue_init_params(max_entries, &_ring, &_ring_params);
        }

        bool ping() {
            io_uring_cqe *cqe_s[max_entries];
            const int cqe_count = io_uring_peek_batch_cqe(&_ring, cqe_s, max_entries);
            for (int i = 0; i < cqe_count; ++i) {
                const auto identity = io_uring_cqe_get_data64(cqe_s[i]);
                runner::reattach(std::move(_waiters[identity]));
                _waiters.erase(cqe_s[i]->user_data);
                _waiter_results.insert({identity, cqe_s[i]->res});
            }
            return not _waiters.empty();
        }

        common::queue<kernel_entity> _submission_queue {kernel_entity::_kernelic_entity_mempool};

        ~kernel_controller() {
            io_uring_queue_exit(&_ring);
        }

        /**
         * @brief Gets submission entity of the io_uring with installed user_data for a waiting task
         * @return A pair of the waiter identity and pointer to submission queue entity
         */
        auto get_sqe_for(async<>&& waiter) -> std::tuple<uintptr_t, io_uring_sqe*> {
            io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
            io_uring_sqe_set_data(sqe, waiter._coroutine.address());
            _waiters.insert({sqe->user_data, std::move(waiter)});
            return std::tie(sqe->user_data, sqe);
        }

        /**
         * @brief Erases waiter's identity from the controller
         * @warning May be used only if SQE wasn't passed to the ring
         */
        void detach_sqe_for(uintptr_t waiter_identity) {
            _waiters.erase(waiter_identity);
        }

        int32_t consume_result(uintptr_t waiter_identity) {
            const auto result = _waiter_results[waiter_identity];
            _waiter_results.erase(waiter_identity);
            return result;
        }

    private:

        io_uring_params _ring_params;
        io_uring _ring;

        std::unordered_map<uintptr_t, async<>> _waiters {};
        std::unordered_map<uintptr_t, int32_t> _waiter_results {};
    };

    thread_local common::slab_mempool<kernel_entity> kernel_entity::_kernelic_entity_mempool =
        common::slab_mempool<kernel_entity>();
}

#endif //ACE_CORE_KERNELIC_H
