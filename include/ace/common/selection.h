#ifndef ACE_COMMON_SELECTION_H
#define ACE_COMMON_SELECTION_H

enum class allocation_type {
    e_static,
    e_on_init,
    e_dynamic
};

enum class vortex_spawn_mode {
    e_shared, ///< Same service task for all threads
    e_unique, ///< Unique service task for each thread
};

#endif // ACE_COMMON_SELECTION_H
