#ifndef ACE_COMMON_ID_ALLOCA_H
#define ACE_COMMON_ID_ALLOCA_H
#include "atomic_stack.h"
#include "nukes/dynamic/mpmc_queue.h"

namespace ace::common {

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

    class context_id_allocator : public id_allocator {

        context_id_allocator() = default;

    public:

        static context_id_allocator& get_instance() {
            static context_id_allocator instance;
            return instance;
        }

    };

}

#endif // ACE_COMMON_ID_ALLOCA_H
