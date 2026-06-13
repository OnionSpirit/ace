/**
 * @file id_alloc.h
 * @brief Lock-free unique ID allocator for coroutine tracing.
 *
 * @details Provides @c id_allocator (a simple monotonically-increasing ID
 * pool with recycled IDs via an MPMC queue) and @c async_id_allocator (a
 * singleton wrapper).  Used by @c promise_traits::setup_trace() to assign
 * unique debug IDs to coroutine instances.
 */
#ifndef ACE_COMMON_ID_ALLOCA_H
#define ACE_COMMON_ID_ALLOCA_H

#include "nukes/dynamic/mpmc_queue.h"

namespace ace::core::tools {

    /**
     * @brief Lock-free unique ID allocator.
     *
     * @details Allocates IDs via an atomic counter.  Released IDs are pushed
     * into an MPMC queue and recycled on subsequent allocations.  This avoids
     * both locks and unbounded counter growth in long-running systems.
     */
    class id_allocator {

        nukes::dynamic::mpmc_queue<std::size_t> _released {};
        std::atomic<std::size_t> _pool {0};

    public:

        id_allocator() = default;

        std::size_t id_alloc() {
            std::size_t id {};
            if (_released.pop(id)) return id;
            return _pool.fetch_add(1, std::memory_order_acq_rel); // NOTE: Post-increment
        }

        void id_free(const std::size_t id) {
            _released.push(id);
        }
    };

    /**
     * @brief Singleton wrapper around @c id_allocator for coroutine tracing.
     *
     * @details Access via @c async_id_allocator::get_instance().  Used by
     * @c promise_traits to assign globally unique trace IDs to each coroutine.
     */
    class async_id_allocator : public id_allocator {

        async_id_allocator() = default;

    public:

        static async_id_allocator& get_instance() {
            static async_id_allocator instance;
            return instance;
        }

    };

}

#endif // ACE_COMMON_ID_ALLOCA_H
