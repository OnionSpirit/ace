#ifndef ACE_PREFETCH_H
#define ACE_PREFETCH_H

#ifdef _MSC_VER
#include <mmintrin.h>
#endif

namespace ace::core::tools {

    enum cache_locality : int {
        e_temporal = 0, ///< Minimal stack pollution
        e_l1_cache = 1, ///< L1 cache
        e_l2_cache = 2, ///< L1|L2 cache
        e_l3_cache = 3, ///< L1|L2|L3 cache
    };

    template <cache_locality locality_v>
    void prefetch(const void* memptr) {
#ifdef _MSC_VER
        if constexpr (locality_v == e_temporal)
            _mm_prefetch(memptr, _MM_HINT_NTA);
        if constexpr (locality_v == e_l1_cache)
            _mm_prefetch(memptr, _MM_HINT_T1);
        if constexpr (locality_v == e_l2_cache)
            _mm_prefetch(memptr, _MM_HINT_T2);
        if constexpr (locality_v == e_l3_cache)
            _mm_prefetch(memptr, _MM_HINT_T0);
#elif defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(memptr, 0, locality_v);
#endif
    }
}

#endif //ACE_PREFETCH_H
