#ifndef ACE_TESTS_ALLOCATION_FAILURE_H
#define ACE_TESTS_ALLOCATION_FAILURE_H

#include <ace/core/tools/queue.h>

/** @brief Scoped current-thread injection at slab allocation or registration. */
struct slab_failure_scope {
    using hook_t = void (*)(bool);
    hook_t previous;
    explicit slab_failure_scope(hook_t hook)
        : previous(ace::core::tools::slab_mempool<int>::set_growth_for_testing(hook)) {}
    ~slab_failure_scope() {
        ace::core::tools::slab_mempool<int>::set_growth_for_testing(previous);
    }
    static void allocation(bool registration) {
        if (not registration) throw std::bad_alloc {};
    }
    static void registration(bool registration) {
        if (registration) throw std::bad_alloc {};
    }
};

/** @brief Scoped service-start allocation failure, before coroutine publication. */
template <typename Service>
struct service_failure_scope {
    service_failure_scope() { Service::set_respawn_for_testing([] { throw std::bad_alloc {}; }); }
    ~service_failure_scope() { Service::set_respawn_for_testing(nullptr); }
};

#endif
