#ifndef ACE_COMMON_SELECTION_H
#define ACE_COMMON_SELECTION_H

enum class allocation_type {
    e_static,
    e_on_init,
    e_dynamic
};

enum class vortex_spawn_mode {
    e_thread_shared, ///< Single vortex for all threads
    e_thread_local,  ///< Local vortex instance for each thread
};

enum transport_entity_state {
    e_indirect = 0,
    e_connected = 1
};

#endif // ACE_COMMON_SELECTION_H
